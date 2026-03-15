// examples/go/main.go — AgentChat Go SDK example
//
// Demonstrates connecting to AgentChat via the REST API (port 8767):
//   1. Health check
//   2. List agents
//   3. List channels
//   4. Create a channel
//   5. Send a message
//   6. Fetch agents again to confirm state
//
// Usage:
//   cd examples/go
//   go run main.go                          # connects to localhost:8767
//   go run main.go -server http://host:8767  # custom server
package main

import (
	"bytes"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"log"
	"net/http"
	"time"
)

// ── Types ────────────────────────────────────────────────────────────────────

type Agent struct {
	ID           uint64 `json:"id"`
	Name         string `json:"name"`
	Capabilities string `json:"capabilities"`
}

type Channel struct {
	ID   uint64 `json:"id"`
	Name string `json:"name"`
	Type int    `json:"type"` // 0=dm 1=group 2=broadcast
}

type AgentsResponse struct {
	Agents []Agent `json:"agents"`
}

type ChannelsResponse struct {
	Channels []Channel `json:"channels"`
}

type CreateChannelRequest struct {
	Name string `json:"name"`
	Type int    `json:"type"`
}

type CreateChannelResponse struct {
	ChannelID uint64 `json:"channel_id"`
}

type SendMessageRequest struct {
	FromAgentID uint64 `json:"from_agent_id"`
	ToChannelID uint64 `json:"to_channel_id"`
	Text        string `json:"text"`
}

type SendMessageResponse struct {
	MessageID uint64 `json:"message_id"`
}

type HealthResponse struct {
	Status string `json:"status"`
}

// ── Client ───────────────────────────────────────────────────────────────────

type Client struct {
	BaseURL    string
	HTTPClient *http.Client
}

func NewClient(baseURL string) *Client {
	return &Client{
		BaseURL: baseURL,
		HTTPClient: &http.Client{Timeout: 10 * time.Second},
	}
}

func (c *Client) get(path string, out interface{}) error {
	resp, err := c.HTTPClient.Get(c.BaseURL + path)
	if err != nil {
		return fmt.Errorf("GET %s: %w", path, err)
	}
	defer resp.Body.Close()
	body, _ := io.ReadAll(resp.Body)
	if resp.StatusCode >= 300 {
		return fmt.Errorf("GET %s: HTTP %d: %s", path, resp.StatusCode, body)
	}
	return json.Unmarshal(body, out)
}

func (c *Client) post(path string, in interface{}, out interface{}) error {
	payload, err := json.Marshal(in)
	if err != nil {
		return err
	}
	resp, err := c.HTTPClient.Post(c.BaseURL+path, "application/json", bytes.NewReader(payload))
	if err != nil {
		return fmt.Errorf("POST %s: %w", path, err)
	}
	defer resp.Body.Close()
	body, _ := io.ReadAll(resp.Body)
	if resp.StatusCode >= 300 {
		return fmt.Errorf("POST %s: HTTP %d: %s", path, resp.StatusCode, body)
	}
	if out != nil {
		return json.Unmarshal(body, out)
	}
	return nil
}

// ── API methods ──────────────────────────────────────────────────────────────

func (c *Client) Health() (*HealthResponse, error) {
	var r HealthResponse
	return &r, c.get("/v1/health", &r)
}

func (c *Client) ListAgents() ([]Agent, error) {
	var r AgentsResponse
	return r.Agents, c.get("/v1/agents", &r)
}

func (c *Client) ListChannels() ([]Channel, error) {
	var r ChannelsResponse
	return r.Channels, c.get("/v1/channels", &r)
}

func (c *Client) CreateChannel(name string, chType int) (uint64, error) {
	var r CreateChannelResponse
	err := c.post("/v1/channels", CreateChannelRequest{Name: name, Type: chType}, &r)
	return r.ChannelID, err
}

func (c *Client) SendMessage(fromAgentID, toChannelID uint64, text string) (uint64, error) {
	var r SendMessageResponse
	err := c.post("/v1/messages", SendMessageRequest{
		FromAgentID: fromAgentID,
		ToChannelID: toChannelID,
		Text:        text,
	}, &r)
	return r.MessageID, err
}

// ── Main demo ────────────────────────────────────────────────────────────────

func main() {
	server := flag.String("server", "http://localhost:8767", "AgentChat REST server base URL")
	flag.Parse()

	client := NewClient(*server)
	log.Printf("Connecting to AgentChat at %s\n", *server)

	// 1. Health check
	health, err := client.Health()
	if err != nil {
		log.Fatalf("Health check failed: %v", err)
	}
	fmt.Printf("✓ Health: %s\n", health.Status)

	// 2. List agents
	agents, err := client.ListAgents()
	if err != nil {
		log.Fatalf("ListAgents failed: %v", err)
	}
	fmt.Printf("✓ Agents (%d):\n", len(agents))
	for _, a := range agents {
		fmt.Printf("   [%d] %s  caps=%s\n", a.ID, a.Name, a.Capabilities)
	}

	// 3. List channels
	channels, err := client.ListChannels()
	if err != nil {
		log.Fatalf("ListChannels failed: %v", err)
	}
	ch_types := map[int]string{0: "dm", 1: "group", 2: "broadcast"}
	fmt.Printf("✓ Channels (%d):\n", len(channels))
	for _, ch := range channels {
		fmt.Printf("   [%d] #%s  type=%s\n", ch.ID, ch.Name, ch_types[ch.Type])
	}

	// 4. Create a channel
	chName := fmt.Sprintf("go-demo-%d", time.Now().Unix())
	chID, err := client.CreateChannel(chName, 1 /* group */)
	if err != nil {
		log.Fatalf("CreateChannel failed: %v", err)
	}
	fmt.Printf("✓ Created channel #%s → id=%d\n", chName, chID)

	// 5. Send a message (requires at least one registered agent)
	if len(agents) > 0 {
		msgID, err := client.SendMessage(agents[0].ID, chID, "Hello from the Go SDK example! 🐹")
		if err != nil {
			log.Printf("SendMessage failed (expected if agent has no messaging capability): %v", err)
		} else {
			fmt.Printf("✓ Sent message → msg_id=%d\n", msgID)
		}
	} else {
		fmt.Println("⚠ No agents registered — skipping SendMessage demo")
		fmt.Println("  Start the server and register an agent first, then re-run this example.")
	}

	fmt.Println("\nDone. See README for full AgentChat protocol documentation.")
}
