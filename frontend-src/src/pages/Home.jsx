import {useState,useEffect,useContext} from 'react';import {api} from '../api.js';import {Ctx} from '../ctx.js';import {CATS} from '../common.js';import {EventCard} from './Events.jsx';
export default function Home(){const {me,go,toast}=useContext(Ctx);
 const [q,setQ]=useState(''),[cat,setCat]=useState(''),[events,setEvents]=useState(null),[stats,setStats]=useState(null);
 useEffect(()=>{const t=setTimeout(()=>api(`/events?q=${encodeURIComponent(q)}&category=${encodeURIComponent(cat)}`).then(r=>setEvents(r.events)).catch(e=>toast(e.message,1)),250);return()=>clearTimeout(t)},[q,cat]);
 useEffect(()=>{api('/public/stats').then(r=>setStats(r.stats)).catch(()=>{})},[]);
 const register=id=>go('register',{id}); // opens the registration form of the selected event (login is asked on that page if needed)
 return <><div className="hero"><h1>Discover. Participate. Achieve.</h1><p>Your campus events, registrations, attendance, and certificates — all in one place.</p>
  <input aria-label="Search events" placeholder="Search events..." value={q} onChange={e=>setQ(e.target.value)}/>
  <div className="chips">{['',...CATS].map(c=><button key={c} className={'chip'+(c===cat?' on':'')} onClick={()=>setCat(c)}>{c||'All'}</button>)}</div>
  {stats&&<div className="hstats"><div><b>{stats.events}</b><span>Live events</span></div><div><b>{stats.registrations}</b><span>Registrations</span></div><div><b>{stats.certificates}</b><span>Certificates issued</span></div></div>}</div>
  <main><h2>Upcoming events</h2><div className="grid">{!events?[1,2,3].map(i=><div key={i} className="sk"/>):events.length?events.map(e=><EventCard key={e.id} e={e} mode="pub" onRegister={register}/>):<div className="empty">No events found. Try another search.</div>}</div></main></>}
