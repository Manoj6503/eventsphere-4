import {useState,useEffect,useContext} from 'react';import {api} from '../api.js';import {Ctx} from '../ctx.js';import {CATS} from '../common.js';
// Student asks the admin for permission to organize events. Organizer access is granted only when the admin approves.
export default function Request(){const {me,setMe,go,toast}=useContext(Ctx);const [d,setD]=useState(undefined),[again,setAgain]=useState(false);
 const [f,setF]=useState({organization:'',event_title:'',event_category:'Technical',event_description:'',expected_date:'',reason:''});const set=k=>e=>setF({...f,[k]:e.target.value});
 const load=()=>api('/organizer/request').then(setD).catch(e=>toast(e.message,1));
 useEffect(()=>{if(me)load();else setD(null)},[me]);
 const refresh=async()=>{try{const u=(await api('/me')).user;setMe(u);await load();toast('Status refreshed')}catch(e){toast(e.message,1)}};
 const submit=async e=>{e.preventDefault();try{toast((await api('/organizer/request','POST',f)).message);setAgain(false);load()}catch(x){toast(x.message,1)}};
 if(!me)return <main><div className="panel" style={{textAlign:'center'}}><h2>Become an event organizer</h2><p>Log in with your student account first, then submit a request for admin approval.</p><button onClick={()=>go('login')}>Login</button> <button className="o" onClick={()=>go('signup')}>Sign up</button></div></main>;
 if(me.role==='admin')return <main><div className="panel" style={{textAlign:'center'}}><h2>You are the administrator</h2><p>Organizer requests are reviewed from the Admin Dashboard.</p><button onClick={()=>go('admin',{tab:'requests'})}>Open requests</button></div></main>;
 if(me.role==='organizer')return <main><div className="panel" style={{textAlign:'center'}}><h2>✅ You are an approved event organizer</h2><p>Create and manage your events from the Organizer Dashboard.</p><button onClick={()=>go('dash')}>Open Organizer Dashboard</button></div></main>;
 if(d===undefined)return <main><div className="sk"/></main>;
 const r=d?.request,st=r?.status;
 if(r&&(st==='pending'||(!again)))return <main><div className="panel" style={{maxWidth:700}}><h2>Organizer request</h2>
  <div className="res" style={st==='pending'?{background:'#fef9c3',color:'#854d0e'}:st==='approved'?{background:'#dcfce7',color:'#166534'}:{background:'#ffe4e6',color:'#9f1239'}}>{st==='pending'?'⏳ Waiting for admin approval. You cannot create events until your request is approved.':st==='approved'?'✅ Approved! Press "Check status" to activate your organizer access.':st==='revoked'?'Your organizer access was removed by the admin.':'❌ Your request was rejected.'}{r.admin_note&&<><br/>Reason: {r.admin_note}</>}</div>
  <div className="meta"><b>{r.event_title}</b> ({r.event_category}) · {r.organization}<br/>{r.event_description}<br/>Submitted {r.created_at}</div><br/>
  {st==='pending'||st==='approved'?<button className="o" onClick={refresh}>Check status</button>:<button onClick={()=>setAgain(true)}>Submit a new request</button>}</div></main>;
 return <main><form className="box" onSubmit={submit}><h2>Request to create events</h2><p className="meta">Tell the admin what you want to organize. Once approved you get access to the Organizer Dashboard.</p>
  <label>Department / club / organization<input value={f.organization} onChange={set('organization')} maxLength={120} required/></label>
  <label>Title of your first event<input value={f.event_title} onChange={set('event_title')} maxLength={150} required/></label>
  <div className="two"><label>Event category<select value={f.event_category} onChange={set('event_category')}>{CATS.map(c=><option key={c}>{c}</option>)}</select></label><label>Expected date<input type="date" value={f.expected_date} onChange={set('expected_date')}/></label></div>
  <label>Event description<textarea rows={3} maxLength={1000} value={f.event_description} onChange={set('event_description')} required/></label>
  <label>Why do you want to organize events?<textarea rows={3} maxLength={600} value={f.reason} onChange={set('reason')} required/></label><br/><button>Submit request</button></form></main>}
