/**
 * AgentChat Node.js SDK
 * Implements the AgentChat binary protocol directly over TCP.
 * Protocol: [4 bytes BE length][1 byte PacketType][payload]
 */
import * as net from 'net';
import * as crypto from 'crypto';

// ── Protocol constants ────────────────────────────────────────────────────────
const PROTOCOL_VERSION = 1;

const PT = {
  HELLO:          0x01,
  HELLO_ACK:      0x02,
  AUTH_OK:        0x04,
  AUTH_FAIL:      0x05,
  AUTH_CHALLENGE: 0x06,
  AUTH_RESPONSE:  0x07,
  SEND_MESSAGE:   0x20,
  RECV_MESSAGE:   0x21,
} as const;

// ── Types ─────────────────────────────────────────────────────────────────────
export interface AgentChatOptions {
  host: string;
  port: number;
  agentId: bigint;
  identityPrivHex?: string;  // Ed25519 private key hex (32 bytes = 64 hex chars)
  exchangePrivHex?: string;  // X25519 private key hex (32 bytes = 64 hex chars)
}

export type MessageCallback = (fromAgentId: bigint, payload: Buffer) => void;

// ── Key generation ────────────────────────────────────────────────────────────

/**
 * Generate an Ed25519 key pair using Node.js built-in crypto.
 * Returns { publicHex, privateHex } as 64-char hex strings (32 bytes each).
 */
export function generateKeyPair(): { publicHex: string; privateHex: string } {
  const { privateKey, publicKey } = crypto.generateKeyPairSync('ed25519');
  const priv = privateKey.export({ type: 'pkcs8', format: 'der' });
  const pub  = publicKey.export({ type: 'spki',  format: 'der' });
  // Raw key bytes are the last 32 bytes of the DER encoding
  const privRaw = priv.slice(priv.length - 32);
  const pubRaw  = pub.slice(pub.length - 32);
  return {
    publicHex:  Buffer.from(pubRaw).toString('hex'),
    privateHex: Buffer.from(privRaw).toString('hex'),
  };
}

/** Derive X25519 key pair */
function generateX25519KeyPair(): { pub: Buffer; priv: Buffer } {
  const { privateKey, publicKey } = crypto.generateKeyPairSync('x25519');
  const priv = privateKey.export({ type: 'pkcs8', format: 'der' }) as Buffer;
  const pub  = publicKey.export({ type: 'spki',  format: 'der' }) as Buffer;
  return {
    priv: Buffer.from(priv.slice(priv.length - 32)),
    pub:  Buffer.from(pub.slice(pub.length - 32)),
  };
}

/** Load Ed25519 private key from raw 32-byte hex, return KeyObject */
function ed25519KeyFromRawHex(privHex: string): crypto.KeyObject {
  const rawPriv = Buffer.from(privHex, 'hex');
  return crypto.createPrivateKey({ key: rawPriv, format: 'der',
    type: 'pkcs8',
    // Build minimal PKCS8 DER wrapper for Ed25519
    // OID 1.3.101.112 = Ed25519
    // We use the full PKCS8 construction
  } as any);
}

/** Build PKCS8 DER for Ed25519 from raw 32-byte private key */
function buildEd25519Pkcs8(rawPriv: Buffer): Buffer {
  // PKCS8 structure for Ed25519:
  // SEQUENCE {
  //   INTEGER 0  (version)
  //   SEQUENCE { OID 1.3.101.112 }  (algorithm)
  //   OCTET STRING { OCTET STRING { rawPriv } }  (private key)
  // }
  const oid = Buffer.from('06032b6570', 'hex');          // OID Ed25519
  const alg = Buffer.concat([Buffer.from('3005', 'hex'), oid]);
  const innerOctet = Buffer.concat([Buffer.from('0420', 'hex'), rawPriv]);
  const outerOctet = Buffer.concat([Buffer.from('0422', 'hex'), innerOctet]);
  const ver = Buffer.from('020100', 'hex');
  const seq = Buffer.concat([ver, alg, outerOctet]);
  const seqLen = seq.length;
  const lenBuf = seqLen < 128
    ? Buffer.from([seqLen])
    : Buffer.from([0x81, seqLen]);
  return Buffer.concat([Buffer.from('30', 'hex'), lenBuf, seq]);
}

/** Sign a 32-byte challenge with Ed25519 private key (raw hex), return 64-byte sig */
function ed25519Sign(privHex: string, challenge: Buffer): Buffer {
  const rawPriv = Buffer.from(privHex, 'hex');
  const pkcs8   = buildEd25519Pkcs8(rawPriv);
  const privKey = crypto.createPrivateKey({ key: pkcs8, format: 'der', type: 'pkcs8' });
  return crypto.sign(null, challenge, privKey) as Buffer;
}

/** Derive Ed25519 public key (raw 32 bytes) from raw private key hex */
function ed25519PubFromPrivHex(privHex: string): Buffer {
  const rawPriv = Buffer.from(privHex, 'hex');
  const pkcs8   = buildEd25519Pkcs8(rawPriv);
  const privKey = crypto.createPrivateKey({ key: pkcs8, format: 'der', type: 'pkcs8' });
  const pubKey  = crypto.createPublicKey(privKey);
  const spki    = pubKey.export({ type: 'spki', format: 'der' }) as Buffer;
  return spki.slice(spki.length - 32);
}

// ── Frame helpers ─────────────────────────────────────────────────────────────

function encodeFrame(type: number, payload: Buffer): Buffer {
  const frame = Buffer.allocUnsafe(4 + 1 + payload.length);
  frame.writeUInt32BE(1 + payload.length, 0);  // BE length includes type byte
  frame.writeUInt8(type, 4);
  payload.copy(frame, 5);
  return frame;
}

/** pack_blob: [4 bytes BE length][data] */
function packBlob(data: Buffer): Buffer {
  const len = Buffer.allocUnsafe(4);
  len.writeUInt32BE(data.length, 0);
  return Buffer.concat([len, data]);
}

/** unpack_blob from buffer at offset, returns [data, newOffset] */
function unpackBlob(buf: Buffer, off: number): [Buffer, number] | null {
  if (off + 4 > buf.length) return null;
  const len = buf.readUInt32BE(off); off += 4;
  if (off + len > buf.length) return null;
  return [buf.slice(off, off + len), off + len];
}

// ── AgentChatClient ───────────────────────────────────────────────────────────

export class AgentChatClient {
  private socket: net.Socket | null = null;
  private messageCallback: MessageCallback | null = null;
  private recvBuf: Buffer = Buffer.alloc(0);
  private readonly opts: AgentChatOptions;

  // Ed25519 identity key
  private identityPrivHex: string;
  private identityPubRaw:  Buffer;

  // X25519 ephemeral key for this session
  private ephemPub:  Buffer;
  private ephemPriv: Buffer;

  constructor(opts: AgentChatOptions) {
    this.opts = opts;

    // Set up Ed25519 identity key
    if (opts.identityPrivHex && opts.identityPrivHex.length === 64) {
      this.identityPrivHex = opts.identityPrivHex;
      this.identityPubRaw  = ed25519PubFromPrivHex(opts.identityPrivHex);
    } else {
      const kp = generateKeyPair();
      this.identityPrivHex = kp.privateHex;
      this.identityPubRaw  = Buffer.from(kp.publicHex, 'hex');
    }

    // Set up X25519 ephemeral key
    if (opts.exchangePrivHex && opts.exchangePrivHex.length === 64) {
      // Derive public from provided private
      const rawPriv = Buffer.from(opts.exchangePrivHex, 'hex');
      // Build PKCS8 for X25519
      const pkcs8 = buildX25519Pkcs8(rawPriv);
      const privKey = crypto.createPrivateKey({ key: pkcs8, format: 'der', type: 'pkcs8' });
      const pubKey  = crypto.createPublicKey(privKey);
      const spki    = pubKey.export({ type: 'spki', format: 'der' }) as Buffer;
      this.ephemPriv = rawPriv;
      this.ephemPub  = spki.slice(spki.length - 32);
    } else {
      const kp = generateX25519KeyPair();
      this.ephemPriv = kp.priv;
      this.ephemPub  = kp.pub;
    }
  }

  async connect(): Promise<void> {
    return new Promise((resolve, reject) => {
      this.recvBuf = Buffer.alloc(0);
      this.socket  = net.createConnection(
        { host: this.opts.host, port: this.opts.port },
        () => this._sendHello()
      );
      this.socket.on('data', (data) => this._onData(data));
      this.socket.on('error', reject);
      this.socket.on('close', () => { this.socket = null; });

      // Resolve once AUTH_OK is received
      this._authResolve = resolve;
      this._authReject  = reject;
    });
  }

  private _authResolve?: () => void;
  private _authReject?:  (e: Error) => void;

  async sendText(toAgentId: bigint, text: string): Promise<void> {
    const textBuf = Buffer.from(text, 'utf-8');
    // SEND_MESSAGE payload: [u64 BE to_agent_id][message bytes]
    const payload = Buffer.allocUnsafe(8 + textBuf.length);
    payload.writeBigUInt64BE(toAgentId, 0);
    textBuf.copy(payload, 8);
    this.socket?.write(encodeFrame(PT.SEND_MESSAGE, payload));
  }

  onMessage(cb: MessageCallback): void {
    this.messageCallback = cb;
  }

  disconnect(): void {
    this.socket?.end();
    this.socket = null;
  }

  private _sendHello(): void {
    // HELLO payload: [u16 BE version][32 bytes X25519 ephemeral pubkey]
    const payload = Buffer.allocUnsafe(2 + 32);
    payload.writeUInt16BE(PROTOCOL_VERSION, 0);
    this.ephemPub.copy(payload, 2);
    this.socket?.write(encodeFrame(PT.HELLO, payload));
  }

  private _sendAuthResponse(challenge: Buffer): void {
    // AUTH_RESPONSE payload: [u64 BE agent_id][32 bytes identity pubkey][64 bytes sig]
    const sig     = ed25519Sign(this.identityPrivHex, challenge);
    const payload = Buffer.allocUnsafe(8 + 32 + 64);
    payload.writeBigUInt64BE(this.opts.agentId, 0);
    this.identityPubRaw.copy(payload, 8);
    sig.copy(payload, 40);
    this.socket?.write(encodeFrame(PT.AUTH_RESPONSE, payload));
  }

  private _onData(data: Buffer): void {
    this.recvBuf = Buffer.concat([this.recvBuf, data]);
    while (this.recvBuf.length >= 5) {
      const length = this.recvBuf.readUInt32BE(0);  // BE
      if (this.recvBuf.length < 4 + length) break;
      const type = this.recvBuf.readUInt8(4);
      const body = this.recvBuf.slice(5, 4 + length);
      this.recvBuf = this.recvBuf.slice(4 + length);
      this._handlePacket(type, body);
    }
  }

  private _handlePacket(type: number, body: Buffer): void {
    switch (type) {
      case PT.HELLO_ACK:
        // Server ephemeral pubkey (32 bytes) — stored for future session encryption
        break;

      case PT.AUTH_CHALLENGE: {
        // blob-prefixed 32-byte challenge
        const r = unpackBlob(body, 0);
        if (!r) return;
        const [challenge] = r;
        this._sendAuthResponse(challenge);
        break;
      }

      case PT.AUTH_OK:
        if (this._authResolve) { this._authResolve(); this._authResolve = undefined; }
        break;

      case PT.AUTH_FAIL:
        if (this._authReject) {
          this._authReject(new Error('AUTH_FAIL from server'));
          this._authReject = undefined;
        }
        break;

      case PT.RECV_MESSAGE: {
        // [u64 BE from_agent_id][payload]
        if (body.length < 8) return;
        const fromId  = body.readBigUInt64BE(0);
        const payload = body.slice(8);
        if (this.messageCallback) this.messageCallback(fromId, payload);
        break;
      }
    }
  }
}

/** Build PKCS8 DER for X25519 from raw 32-byte private key */
function buildX25519Pkcs8(rawPriv: Buffer): Buffer {
  // OID 1.3.101.110 = X25519
  const oid = Buffer.from('06032b656e', 'hex');
  const alg = Buffer.concat([Buffer.from('3005', 'hex'), oid]);
  const innerOctet = Buffer.concat([Buffer.from('0420', 'hex'), rawPriv]);
  const outerOctet = Buffer.concat([Buffer.from('0422', 'hex'), innerOctet]);
  const ver = Buffer.from('020100', 'hex');
  const seq = Buffer.concat([ver, alg, outerOctet]);
  const seqLen = seq.length;
  const lenBuf = seqLen < 128
    ? Buffer.from([seqLen])
    : Buffer.from([0x81, seqLen]);
  return Buffer.concat([Buffer.from('30', 'hex'), lenBuf, seq]);
}
