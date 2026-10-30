// Every call goes to the C++ REST API; the session cookie is sent automatically.
export async function api(path,method='GET',body){
 const r=await fetch('/api'+path,{method,headers:{'Content-Type':'application/json'},body:body?JSON.stringify(body):undefined});
 const j=await r.json().catch(()=>({}));if(!r.ok)throw new Error(j.error||'Request failed');return j}
