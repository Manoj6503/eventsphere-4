// Small pieces shared by the Admin and Organizer dashboards (so nothing is duplicated)
export function Tabs({tabs,cur,onPick}){return <div className="tabs" role="tablist">{tabs.map(([k,l,badge])=><button key={k} role="tab" aria-selected={k===cur} className={'tab'+(k===cur?' on':'')} onClick={()=>onPick(k)}>{l}{badge>0&&<b className="bc">{badge}</b>}</button>)}</div>}

// registration answers are stored as [{key,label,value}] so they always show what the student was actually asked
export function parseAnswers(s){try{const a=JSON.parse(s||'[]');return Array.isArray(a)?a:[]}catch{return []}}
export function Answers({s}){const a=parseAnswers(s);return a.length?<div className="ans">{a.map(x=><div key={x.key}><small>{x.label}:</small> {x.value}</div>)}</div>:<small>-</small>}

export function downloadCsv(name,rows){const b=new Blob([rows.map(r=>r.map(c=>'"'+String(c??'').replace(/"/g,'""')+'"').join(',')).join('\n')],{type:'text/csv'});
 const a=document.createElement('a');a.href=URL.createObjectURL(b);a.download=name;a.click()}
