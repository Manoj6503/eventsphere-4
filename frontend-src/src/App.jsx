import {useState,useEffect,useCallback,useContext} from 'react';
import {api} from './api.js';import {Ctx} from './ctx.js';
import Home from './pages/Home.jsx';import Auth from './pages/Auth.jsx';import Registrations from './pages/Registrations.jsx';
import Admin from './pages/Admin.jsx';import Verify from './pages/Verify.jsx';
import Organizer from './pages/Organizer.jsx';import Register from './pages/Register.jsx';import Request from './pages/Request.jsx';

// Shown after a student scans the organizer's common QR
function CheckIn({result:j,error}){const {go}=useContext(Ctx);
 if(error)return <main><div className="panel" style={{textAlign:'center'}}><h2>❌ {error}</h2><button onClick={()=>go('home')}>Home</button></div></main>;
 return <main><div className="panel" style={{textAlign:'center'}}><h2>{j.result==='ok'?'✅ Attendance marked':'⚠️ Already marked'}</h2><p>{j.name} ({j.reg_code})</p>
  {j.certificate_pending&&<p>Your certificate will be issued as soon as the admin sets the certificate authority.</p>}
  {j.certificate&&<><p>Your certificate is ready.</p><a className="btn" style={{textDecoration:'none',display:'inline-block'}} href={`/api/certificates/${j.certificate}/download`}>Download certificate</a> </>}
  <button className="o" onClick={()=>go('mine')}>My registrations</button></div></main>}

function Navbar({me,unread,go,onBell,logout}){const r=me?.role;
 return <nav><span className="logo" tabIndex={0} onClick={()=>go('home')}>🎪 EventSphere</span><span id="nav">{me?<>
  <button className="o" onClick={()=>go('home')}>Events</button>
  <button className="o" onClick={onBell} aria-label="Notifications">🔔{unread>0&&<b className="bc">{unread}</b>}</button>
  {r==='student'&&<><button className="o" onClick={()=>go('mine')}>My Registrations</button><button className="o" onClick={()=>go('request')}>Become an Organizer</button></>}
  {r==='organizer'&&<button className="o" onClick={()=>go('dash')}>Organizer Dashboard</button>}
  {r==='admin'&&<button className="o" onClick={()=>go('admin')}>Admin Dashboard</button>}
  <button onClick={logout}>Logout ({me.name})</button></>:<><button className="o" onClick={()=>go('login')}>Login</button><button onClick={()=>go('signup')}>Sign up</button></>}</span></nav>}

// Only two dashboards exist: 'admin' and 'dash' (organizer). The older myev/form/scan screens now live inside them as tabs (see go()).
const PAGES={home:Home,login:Auth,signup:Auth,mine:Registrations,dash:Organizer,admin:Admin,verify:Verify,in:CheckIn,register:Register,request:Request};
const NEEDS={dash:'organizer',admin:'admin',mine:'student'}; // front-end guard; the server enforces the same rules on every API call

function Denied({me,need,go}){return <main><div className="panel" style={{textAlign:'center'}}><h2>🔒 {me?'Access denied':'Please log in'}</h2>
 <p>{me?`This page is only for ${need==='organizer'?'approved event organizers':need==='admin'?'administrators':'students'}.`:'You need to log in to see this page.'}</p>
 {!me?<button onClick={()=>go('login')}>Login</button>:me.role==='student'&&need==='organizer'?<button onClick={()=>go('request')}>Request organizer access</button>:<button onClick={()=>go('home')}>Home</button>}</div></main>}

export default function App(){
 const [me,setMe]=useState(null),[ready,setReady]=useState(false),[view,setView]=useState({name:'home'}),[t,setT]=useState(null);
 const [notes,setNotes]=useState([]),[unread,setUnread]=useState(0),[panel,setPanel]=useState(false);
 const toast=useCallback((m,err)=>{setT({m,err});setTimeout(()=>setT(null),3200)},[]);
 const go=(name,data)=>{setPanel(false);const tab={myev:'events',form:'create',scan:'attendance'}[name];
  if(tab)return setView({name:me?.role==='admin'?'admin':'dash',data:{tab,ev:data}});setView({name,data})};
 useEffect(()=>{api('/me').then(r=>setMe(r.user)).catch(()=>{}).finally(()=>setReady(true))},[]);
 // notifications: poll every 20 s (+ browser notification if permitted)
 useEffect(()=>{if(!me){setNotes([]);setUnread(0);return}let last=null;
  const poll=async()=>{try{const m=await api('/me');if(m.user&&m.user.role!==me.role){setMe(m.user);toast('Your account access was updated')}const r=await api('/notifications');setNotes(r.notifications);setUnread(r.unread);
   if(last!==null&&r.unread>last&&window.Notification&&Notification.permission==='granted'&&r.notifications[0])new Notification('EventSphere',{body:r.notifications[0].message});last=r.unread}catch{}};
  poll();const id=setInterval(poll,20000);return()=>clearInterval(id)},[me]);
 // common attendance QR: link looks like /#in=<eventId>.<token>
 useEffect(()=>{if(!ready)return;const m=location.hash.match(/^#in=(\d+)\.([0-9a-f]+)/);if(!m)return;
  if(!me){toast('Please log in to mark your attendance');setView({name:'login'});return}
  history.replaceState(null,'',location.pathname);
  api('/attendance/self','POST',{event_id:+m[1],token:m[2]}).then(result=>setView({name:'in',result})).catch(e=>setView({name:'in',error:e.message}))},[me,ready]);
 const bell=()=>{if(!panel){if(window.Notification&&Notification.permission==='default')Notification.requestPermission();api('/notifications/read','POST').then(()=>setUnread(0)).catch(()=>{})}setPanel(!panel)};
 const logout=()=>api('/logout','POST').then(()=>{setMe(null);go('home')});
 const Page=PAGES[view.name]||Home,need=NEEDS[view.name];
 return <Ctx.Provider value={{me,setMe,go,toast,unread}}>
  <Navbar me={me} unread={unread} go={go} onBell={bell} logout={logout}/>
  {panel&&<div className="np"><b>Notifications</b>{notes.length?notes.map(n=><div key={n.id} className="meta nrow" style={{fontWeight:n.is_read?400:700}}>{n.message}<br/><small>{n.created_at}</small></div>):<div className="empty">No notifications</div>}</div>}
  {need&&!ready?null:need&&me?.role!==need?<Denied me={me} need={need} go={go}/>:<Page mode={view.name} data={view.data} result={view.result} error={view.error}/>}
  <footer>EventSphere · About · Contact · Help · Privacy · <a href="#" onClick={e=>{e.preventDefault();go('verify')}}>Verify certificate</a></footer>
  <div id="toast" role="status" className={t?(t.err?'on err':'on'):''}>{t?.m}</div>
 </Ctx.Provider>}
