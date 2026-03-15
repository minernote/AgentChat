/* AgentChat — app.js */

(function initThree() {
  if (typeof THREE === 'undefined') return;
  const canvas = document.getElementById('three-canvas');
  if (!canvas) return;
  const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: true });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
  renderer.setClearColor(0x000000, 0);
  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(60, 1, 0.1, 1000);
  camera.position.z = 180;
  const mouse = { x: 0, y: 0, tx: 0, ty: 0 };
  window.addEventListener('mousemove', e => {
    mouse.tx = (e.clientX / window.innerWidth - 0.5) * 40;
    mouse.ty = (e.clientY / window.innerHeight - 0.5) * -20;
  });
  const N = 80, SP = 120;
  const pos = [], vel = [];
  for (let i = 0; i < N; i++) {
    pos.push([(Math.random()-.5)*SP*2,(Math.random()-.5)*SP,(Math.random()-.5)*SP]);
    vel.push([(Math.random()-.5)*.04,(Math.random()-.5)*.04,(Math.random()-.5)*.02]);
  }
  const geom = new THREE.BufferGeometry();
  const pa = new Float32Array(N*3);
  for (let i=0;i<N;i++){pa[i*3]=pos[i][0];pa[i*3+1]=pos[i][1];pa[i*3+2]=pos[i][2];}
  geom.setAttribute('position', new THREE.BufferAttribute(pa,3));
  scene.add(new THREE.Points(geom, new THREE.PointsMaterial({color:0x00d4ff,size:2.5,transparent:true,opacity:.85,sizeAttenuation:true})));
  const lineMat = new THREE.LineBasicMaterial({color:0x00d4ff,transparent:true,opacity:.12});
  const lineGeom = new THREE.BufferGeometry();
  const ML = N*6, la = new Float32Array(ML*2*3);
  lineGeom.setAttribute('position', new THREE.BufferAttribute(la,3));
  const lineObj = new THREE.LineSegments(lineGeom, lineMat);
  scene.add(lineObj);
  const ag = new THREE.BufferGeometry();
  const ap = new Float32Array(30*3);
  for(let i=0;i<30;i++){ap[i*3]=(Math.random()-.5)*SP*2;ap[i*3+1]=(Math.random()-.5)*SP;ap[i*3+2]=(Math.random()-.5)*SP;}
  ag.setAttribute('position',new THREE.BufferAttribute(ap,3));
  scene.add(new THREE.Points(ag,new THREE.PointsMaterial({color:0x7b2ff7,size:1.8,transparent:true,opacity:.7,sizeAttenuation:true})));
  function resize(){
    const w=canvas.offsetWidth||window.innerWidth,h=canvas.offsetHeight||window.innerHeight;
    renderer.setSize(w,h,false);camera.aspect=w/h;camera.updateProjectionfederated messaging protocol();
  }
  resize(); window.addEventListener('resize',resize);
  const CD=40; let frame=0;
  function animate(){
    requestAnimationFrame(animate); frame++;
    mouse.x+=(mouse.tx-mouse.x)*.04; mouse.y+=(mouse.ty-mouse.y)*.04;
    scene.rotation.y=mouse.x*.003; scene.rotation.x=mouse.y*.003;
    scene.rotation.z=Math.sin(frame*.002)*.04;
    const pattr=geom.attributes.position.array;
    for(let i=0;i<N;i++){
      pos[i][0]+=vel[i][0];pos[i][1]+=vel[i][1];pos[i][2]+=vel[i][2];
      if(Math.abs(pos[i][0])>SP)vel[i][0]*=-1;
      if(Math.abs(pos[i][1])>SP*.6)vel[i][1]*=-1;
      if(Math.abs(pos[i][2])>SP*.6)vel[i][2]*=-1;
      pattr[i*3]=pos[i][0];pattr[i*3+1]=pos[i][1];pattr[i*3+2]=pos[i][2];
    }
    geom.attributes.position.needsUpdate=true;
    let lc=0;
    for(let i=0;i<N&&lc<ML;i++){
      for(let j=i+1;j<N&&lc<ML;j++){
        const dx=pos[i][0]-pos[j][0],dy=pos[i][1]-pos[j][1],dz=pos[i][2]-pos[j][2];
        if(Math.sqrt(dx*dx+dy*dy+dz*dz)<CD){
          la[lc*6]=pos[i][0];la[lc*6+1]=pos[i][1];la[lc*6+2]=pos[i][2];
          la[lc*6+3]=pos[j][0];la[lc*6+4]=pos[j][1];la[lc*6+5]=pos[j][2];
          lc++;
        }
      }
    }
    lineGeom.setDrawRange(0,lc*2);
    lineGeom.attributes.position.needsUpdate=true;
    lineMat.opacity=0.08+Math.min(lc/ML,1)*.15;
    renderer.render(scene,camera);
  }
  animate();
})();

const nav=document.getElementById('nav');
window.addEventListener('scroll',()=>nav.classList.toggle('scrolled',window.scrollY>40),{passive:true});

const hb=document.getElementById('hamburger'),mm=document.getElementById('mobileMenu');
if(hb&&mm){
  hb.addEventListener('click',()=>mm.classList.toggle('open'));
  mm.querySelectorAll('.mobile-link').forEach(l=>l.addEventListener('click',()=>mm.classList.remove('open')));
}

const io=new IntersectionObserver(entries=>{
  entries.forEach(e=>{if(e.isIntersecting){e.target.classList.add('visible');io.unobserve(e.target);}});
},{threshold:0.12});
document.querySelectorAll('.reveal').forEach(el=>io.observe(el));

const fnames={python:'agent.py',nodejs:'agent.ts',server:'build.sh'};
document.querySelectorAll('.tab').forEach(btn=>{
  btn.addEventListener('click',()=>{
    const t=btn.dataset.tab;
    document.querySelectorAll('.tab').forEach(b=>b.classList.remove('on'));
    document.querySelectorAll('.code-block').forEach(b=>b.classList.remove('on'));
    btn.classList.add('on');
    const bl=document.getElementById('tab-'+t);
    if(bl)bl.classList.add('on');
    const fn=document.getElementById('termFname');
    if(fn)fn.textContent=fnames[t]||'';
  });
});

document.querySelectorAll('a[href^="#"]').forEach(a=>{
  a.addEventListener('click',e=>{
    const t=document.querySelector(a.getAttribute('href'));
    if(t){e.preventDefault();t.scrollIntoView({behavior:'smooth',block:'start'});}
  });
});
