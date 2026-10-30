import {useState,useContext} from 'react';import {api} from '../api.js';
export default function Verify(){const [id,setId]=useState(''),[r,setR]=useState(null);
 const submit=async e=>{e.preventDefault();try{setR({ok:await api('/certificates/verify/'+encodeURIComponent(id.trim()))})}catch(err){setR({err:err.message})}};
 return <main><form className="box" onSubmit={submit}><h2>Verify a certificate</h2><label>Certificate ID<input placeholder="CERT-XXXXXXXXXX" value={id} onChange={e=>setId(e.target.value)} required/></label><br/><button>Verify</button>
  {r&&<div className="meta" style={{marginTop:14}}>{r.ok?<>✅ <b>Valid certificate</b><br/>{r.ok.name} attended <b>{r.ok.event}</b> on {r.ok.date}<br/>Issued {r.ok.issued}</>:'❌ '+r.err}</div>}</form></main>}
