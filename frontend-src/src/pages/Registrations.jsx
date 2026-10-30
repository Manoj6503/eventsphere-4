import {useState,useEffect,useContext} from 'react';import {QRCodeSVG} from 'qrcode.react';import {api} from '../api.js';import {Ctx} from '../ctx.js';
export default function Registrations(){const {toast,unread}=useContext(Ctx);const [r,setR]=useState(null),[qr,setQr]=useState(null);
 const load=()=>api('/my/registrations').then(x=>setR(x.registrations)).catch(e=>toast(e.message,1));
 useEffect(()=>{load()},[unread]); // reloads when a new notification (e.g. certificate ready) arrives
 const cancel=async id=>{if(!confirm('Cancel this registration?'))return;try{await api(`/registrations/${id}/cancel`,'POST');toast('Cancelled');load()}catch(e){toast(e.message,1)}};
 if(!r)return <main><div className="sk"/></main>;
 return <main><h2>My registrations</h2>{r.length?<table><thead><tr><th>ID</th><th>Event</th><th>Date</th><th>Status</th><th>Attendance</th><th>QR / Certificate</th></tr></thead><tbody>
  {r.map(x=><tr key={x.id}><td>{x.reg_code}</td><td>{x.title}</td><td>{x.event_date}</td><td className="st">{x.status}</td><td>{x.checked_in_at?'✅ Present':'-'}</td><td>
   {x.cert_id?<><a className="btn" style={{textDecoration:'none',display:'inline-block'}} href={`/api/certificates/${x.cert_id}/download`}>Download certificate</a> <small>email: {x.email_status}</small></>
   :x.checked_in_at?<small>Attended. Your certificate will appear here once the admin sets the certificate authority.</small>:x.status!=='confirmed'?null:<>{x.is_today?<button onClick={()=>setQr(x)}>Show QR pass</button>:<small>QR opens on {x.event_date} </small>}<button className="d" onClick={()=>cancel(x.id)}>Cancel</button></>}</td></tr>)}</tbody></table>
  :<div className="empty">You have not registered for any events yet.</div>}
  {qr&&<div className="panel" style={{textAlign:'center',marginTop:20}}><h3>{qr.title}</h3><div className="qrbox"><QRCodeSVG value={qr.qr_token} size={220}/></div><p><b>{qr.reg_code}</b><br/>Show this QR to the organizer at entry</p></div>}</main>}
