import {useState,useContext} from 'react';import {api} from '../api.js';import {Ctx} from '../ctx.js';
export default function Auth({mode}){const {setMe,go,toast}=useContext(Ctx);const login=mode==='login';
 const [f,setF]=useState({name:'',email:'',password:''});const set=k=>e=>setF({...f,[k]:e.target.value});
 const submit=async e=>{e.preventDefault();try{
  if(!login){await api('/signup','POST',{...f,role:'student'});toast('Account created, please log in');return go('login')}
  await api('/login','POST',{email:f.email,password:f.password});const u=(await api('/me')).user;setMe(u);
  let next=null;try{next=JSON.parse(sessionStorage.getItem('es_next')||'null');sessionStorage.removeItem('es_next')}catch{}
  if(next)go(next.name,next.data);else go(u.role==='admin'?'admin':u.role==='organizer'?'dash':'home')}catch(err){toast(err.message,1)}};
 return <main><form className="box" onSubmit={submit}><h2>{login?'Welcome back':'Create account'}</h2>
  {!login&&<label>Full name<input value={f.name} onChange={set('name')} required/></label>}
  <label>Email<input type="email" value={f.email} onChange={set('email')} required/></label>
  <label>Password (8+ chars)<input type="password" minLength={8} value={f.password} onChange={set('password')} required/></label>
  {!login&&<p className="meta">Student accounts can register for events. To organize events, request organizer access after signing up; an admin must approve it.</p>}
  <br/><button>{login?'Login':'Sign up'}</button></form></main>}
