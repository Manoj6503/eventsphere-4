#include "httplib.h"
#include "nlohmann/json.hpp"
#include <sqlite3.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <fstream>
#include <sstream>
#include <mutex>
#include <memory>
#include <map>
#include <set>
#include <functional>
#include <filesystem>
#include <thread>
#include <cstring>
#include <ctime>
#include <openssl/hmac.h>
#include <curl/curl.h>
#include <array>
#include <algorithm>
#include <cctype>
using namespace std; using json=nlohmann::json; using namespace httplib;
struct HttpErr{int code;string msg;};
// ---- security helpers ----
string hex(const unsigned char*d,size_t n){static const char*h="0123456789abcdef";string s;for(size_t i=0;i<n;i++){s+=h[d[i]>>4];s+=h[d[i]&15];}return s;}
string rnd(int n){vector<unsigned char>b(n);RAND_bytes(b.data(),n);return hex(b.data(),n);}
string hashPw(const string&pw,const string&salt){unsigned char o[32];PKCS5_PBKDF2_HMAC(pw.c_str(),pw.size(),(const unsigned char*)salt.data(),salt.size(),100000,EVP_sha256(),32,o);return salt+"$"+hex(o,32);}
bool checkPw(const string&pw,const string&st){return hashPw(pw,st.substr(0,st.find('$')))==st;}
// ---- RAII database wrapper (parameterized queries only) ----
class Db{sqlite3*d=nullptr;public:
 explicit Db(const string&p){if(sqlite3_open(p.c_str(),&d))throw runtime_error("cannot open db");}
 ~Db(){sqlite3_close(d);} Db(const Db&)=delete; Db&operator=(const Db&)=delete;
 void exec(const string&sql){char*e=nullptr;if(sqlite3_exec(d,sql.c_str(),0,0,&e)){string m=e;sqlite3_free(e);throw runtime_error(m);}}
 json q(const string&sql,const vector<string>&p={}){sqlite3_stmt*s;if(sqlite3_prepare_v2(d,sql.c_str(),-1,&s,0))throw runtime_error(sqlite3_errmsg(d));
  for(size_t i=0;i<p.size();i++)sqlite3_bind_text(s,i+1,p[i].c_str(),-1,SQLITE_TRANSIENT);
  json r=json::array();int rc;
  while((rc=sqlite3_step(s))==SQLITE_ROW){json o;for(int c=0;c<sqlite3_column_count(s);c++){string n=sqlite3_column_name(s,c);int t=sqlite3_column_type(s,c);
   if(t==SQLITE_INTEGER)o[n]=sqlite3_column_int64(s,c);else if(t==SQLITE_NULL)o[n]=nullptr;else o[n]=string((const char*)sqlite3_column_text(s,c));}r.push_back(o);}
  sqlite3_finalize(s);if(rc!=SQLITE_DONE)throw runtime_error(sqlite3_errmsg(d));return r;}
 long long lastId(){return sqlite3_last_insert_rowid(d);}
};
// ---- User hierarchy: runtime polymorphism for permissions ----
struct User{long long id;string name,email,role;virtual ~User()=default;
 virtual bool canReview()const{return false;} virtual bool canManageEvents()const{return false;} virtual bool canRegister()const{return false;}};
struct Student:User{bool canRegister()const override{return true;}};
struct Organizer:User{bool canManageEvents()const override{return true;}};
struct Admin:User{bool canReview()const override{return true;} bool canManageEvents()const override{return true;}};
unique_ptr<User> makeUser(const json&j){unique_ptr<User>u;string r=j["role"];
 if(r=="admin")u=make_unique<Admin>();else if(r=="organizer")u=make_unique<Organizer>();else u=make_unique<Student>();
 u->id=j["id"];u->name=j["name"];u->email=j["email"];u->role=r;return u;}

string genv(const char*k,const char*d=""){const char*v=getenv(k);return string(v?v:d);}
string b64(const string&s){string o(4*((s.size()+2)/3)+1,'\0');int n=EVP_EncodeBlock((unsigned char*)&o[0],(const unsigned char*)s.data(),s.size());o.resize(n);string r;for(size_t i=0;i<o.size();i+=76)r+=o.substr(i,76)+"\r\n";return r;}
string pdfEsc(const string&s){string o;for(unsigned char c:s){if(c>126||c<32)o+='?';else{if(c=='('||c==')'||c=='\\')o+='\\';o+=(char)c;}}return o;}
string unb64(const string&in){string c;for(char ch:in)if(isalnum((unsigned char)ch)||ch=='+'||ch=='/'||ch=='=')c+=ch;if(c.empty()||c.size()%4)return "";
 string o(c.size()/4*3,'\0');int n=EVP_DecodeBlock((unsigned char*)&o[0],(const unsigned char*)c.data(),c.size());if(n<0)return "";
 size_t pad=(c.back()=='=')+(c.size()>1&&c[c.size()-2]=='=');if((size_t)n<pad)return "";o.resize(n-pad);return o;}
// Reads width/height/components from a JPEG header; false if the bytes are not a usable JPEG
bool jpegInfo(const string&j,int&w,int&h,int&comp){auto B=[&](size_t i){return (int)(unsigned char)j[i];};if(j.size()<12||B(0)!=0xFF||B(1)!=0xD8)return false;size_t i=2;
 while(i+9<j.size()){if(B(i)!=0xFF){i++;continue;}int m=B(i+1);if(m==0xFF){i++;continue;}if(m==0xD8||m==0x01||(m>=0xD0&&m<=0xD7)){i+=2;continue;}
  if(m>=0xC0&&m<=0xCF&&m!=0xC4&&m!=0xC8&&m!=0xCC){h=(B(i+5)<<8)|B(i+6);w=(B(i+7)<<8)|B(i+8);comp=B(i+9);return w>0&&h>0&&w<=8000&&h<=8000&&(comp==1||comp==3);}
  i+=2+((B(i+2)<<8)|B(i+3));}return false;}
// Helvetica / Helvetica-Bold advance widths (1/1000 em, ASCII 32..126) so centred text is really centred
double textW(const string&t,bool bold){static const int R[95]={278,278,355,556,556,889,667,191,333,333,389,584,278,333,278,278,556,556,556,556,556,556,556,556,556,556,278,278,584,584,584,556,1015,667,667,722,722,667,611,778,722,278,500,667,556,833,722,778,667,778,722,667,611,722,667,944,667,667,611,278,278,278,469,556,333,556,556,500,556,556,278,556,556,222,222,500,222,833,556,556,556,556,333,500,278,556,500,722,500,500,500,334,260,334,584},B[95]={278,333,474,556,556,889,722,238,333,333,389,584,278,333,278,278,556,556,556,556,556,556,556,556,556,556,333,333,584,584,584,611,975,722,722,722,722,667,611,778,722,278,556,722,611,833,722,778,667,778,722,667,611,722,667,944,667,667,611,333,278,333,584,556,333,556,611,556,611,556,333,611,611,278,278,556,278,889,611,611,611,611,389,556,333,611,556,778,556,556,500,389,280,389,584};double w=0;for(unsigned char c:t)w+=(c<32||c>126)?556:(bold?B[c-32]:R[c-32]);return w/1000.0;}
// Builds a one-page landscape PDF certificate by hand (no external PDF library needed).
// The signature block only ever shows what the admin configured: authority name, designation and (optionally) the uploaded signature image.
string makePdf(const string&name,const string&ev,const string&date,const string&cid,const string&college,const string&issued,const string&authName,const string&authTitle,const string&sigB64){
 ostringstream c;string jpg=unb64(sigB64);int iw=0,ih=0,ic=0;bool img=!jpg.empty()&&jpegInfo(jpg,iw,ih,ic);
 auto T=[&](const string&t,double cx,double y,double sz,const char*f,double r,double g,double b){bool bd=f[1]=='2';double fit=textW(t,bd);if(fit*sz>640)sz=640/fit;double w=fit*sz;c<<"BT /"<<f<<" "<<sz<<" Tf "<<r<<" "<<g<<" "<<b<<" rg "<<(cx-w/2)<<" "<<y<<" Td ("<<pdfEsc(t)<<") Tj ET\n";};
 c<<"0.43 0.16 0.85 rg 24 507 794 64 re f\n0.31 0.27 0.9 RG 4 w 24 24 794 547 re S\n0.93 0.28 0.6 RG 1.5 w 34 34 774 527 re S\n";
 T(college,421,532,22,"F2",1,1,1);
 T("CERTIFICATE OF PARTICIPATION",421,458,34,"F2",0.06,0.1,0.24);
 T("This is to certify that",421,415,16,"F1",0.3,0.3,0.4);
 T(name,421,362,36,"F2",0.43,0.16,0.85);
 T("has successfully participated in",421,318,16,"F1",0.3,0.3,0.4);
 T(ev,421,272,26,"F2",0.06,0.1,0.24);
 T("held on "+date,421,238,16,"F1",0.3,0.3,0.4);
 if(img){double s=min(180.0/iw,62.0/ih),dw=iw*s,dh=ih*s;c<<"q "<<dw<<" 0 0 "<<dh<<" "<<(220-dw/2)<<" 128 cm /Im1 Do Q\n";}
 c<<"0.2 w 0 0 0 RG 110 125 m 330 125 l S 512 125 m 732 125 l S\n";
 T(authName,220,108,13,"F2",0.06,0.1,0.24);T(authTitle,220,92,11,"F1",0.3,0.3,0.4);
 T(issued,622,132,13,"F2",0.06,0.1,0.24);T("Date of Issue",622,108,12,"F1",0.3,0.3,0.4);
 T("Certificate ID: "+cid,421,58,11,"F1",0.4,0.4,0.5);
 string s=c.str(),out="%PDF-1.4\n";vector<size_t> off;
 auto obj=[&](const string&b){off.push_back(out.size());out+=to_string(off.size())+" 0 obj\n"+b+"\nendobj\n";};
 obj("<</Type/Catalog/Pages 2 0 R>>");obj("<</Type/Pages/Kids[3 0 R]/Count 1>>");
 obj(string("<</Type/Page/Parent 2 0 R/MediaBox[0 0 842 595]/Resources<</Font<</F1 4 0 R/F2 5 0 R>>")+(img?"/XObject<</Im1 7 0 R>>":"")+">>/Contents 6 0 R>>");
 obj("<</Type/Font/Subtype/Type1/BaseFont/Helvetica/Encoding/WinAnsiEncoding>>");obj("<</Type/Font/Subtype/Type1/BaseFont/Helvetica-Bold/Encoding/WinAnsiEncoding>>");
 obj("<</Length "+to_string(s.size())+">>\nstream\n"+s+"endstream");
 if(img)obj("<</Type/XObject/Subtype/Image/Width "+to_string(iw)+"/Height "+to_string(ih)+"/ColorSpace/"+(ic==1?"DeviceGray":"DeviceRGB")+"/BitsPerComponent 8/Filter/DCTDecode/Length "+to_string(jpg.size())+">>\nstream\n"+jpg+"\nendstream");
 size_t x=out.size();out+="xref\n0 "+to_string(off.size()+1)+"\n0000000000 65535 f \n";char buf[32];for(size_t o:off){snprintf(buf,32,"%010zu 00000 n \n",o);out+=buf;}
 out+="trailer\n<</Size "+to_string(off.size()+1)+"/Root 1 0 R>>\nstartxref\n"+to_string(x)+"\n%%EOF\n";return out;}
// ---- registration forms: one template per event category (+ organizer's own extra questions) ----
json FD(const char*k,const char*l,const char*t,bool req,json opts=json::array(),int mn=0,int mx=0){json f={{"key",k},{"label",l},{"type",t},{"required",req}};if(!opts.empty())f["options"]=opts;if(mx){f["min"]=mn;f["max"]=mx;}return f;}
const json& commonFields(){static const json c=json::array({FD("roll_no","Roll / Register number","text",true),FD("department","Department","text",true),
 FD("year","Year of study","select",true,json::array({"1st year","2nd year","3rd year","4th year","5th year / PG"})),FD("phone","Mobile number","tel",true)});return c;}
const map<string,json>& formTemplates(){static const map<string,json> T=[]{map<string,json> m;json IT=json::array({"Individual","Team"});
 m["Technical"]=json::array({FD("participation","Participation type","select",true,IT),FD("team_name","Team name (if team)","text",false),FD("team_size","Team size","number",false,json::array(),1,10),
  FD("skill_level","Skill level","select",true,json::array({"Beginner","Intermediate","Advanced"})),FD("tech_known","Technologies you know","text",false),FD("portfolio","GitHub / portfolio link","text",false)});
 m["Workshop"]=json::array({FD("prior_experience","Prior experience with the topic","select",true,json::array({"None","Basic","Intermediate","Advanced"})),
  FD("laptop","Will you bring a laptop?","select",true,json::array({"Yes","No"})),FD("expectations","What do you want to learn?","textarea",false)});
 m["Cultural"]=json::array({FD("performance_type","Performance type","select",true,json::array({"Dance","Singing","Instrumental","Drama / Skit","Fine arts","Other"})),
  FD("item_title","Title of your item / performance","text",true),FD("group_size","Number of performers","number",true,json::array(),1,50),FD("duration","Duration (minutes)","number",true,json::array(),1,30),
  FD("stage_needs","Stage / sound / props required","textarea",false)});
 m["Symposium"]=json::array({FD("college","College / Institution","text",true),FD("role","Participating as","select",true,json::array({"Attendee","Paper presenter","Poster presenter"})),
  FD("paper_title","Paper / poster title (presenters only)","text",false),FD("abstract","Short abstract (presenters only)","textarea",false),FD("co_authors","Co-authors","text",false)});
 m["Sports"]=json::array({FD("team_category","Category","select",true,json::array({"Men","Women","Mixed"})),FD("participation","Participation type","select",true,IT),FD("team_name","Team name (if team)","text",false),
  FD("position","Preferred position / role","text",false),FD("jersey_size","Jersey size","select",true,json::array({"S","M","L","XL","XXL"})),FD("emergency_contact","Emergency contact number","tel",true),
  FD("fitness","I declare that I am medically fit to participate","checkbox",true)});
 m["Seminar"]=json::array({FD("heard_from","How did you hear about this seminar?","select",false,json::array({"Poster","Friend","Social media","Faculty","Other"})),FD("questions","Questions / topics you would like covered","textarea",false)});
 m["Hackathon"]=json::array({FD("team_name","Team name","text",true),FD("team_size","Team size","number",true,json::array(),1,6),FD("team_members","Team members (names and roll numbers)","textarea",true),
  FD("tech_stack","Planned tech stack","text",false),FD("hack_experience","Hackathon experience","select",true,json::array({"First hackathon","1-3 hackathons","4 or more"}))});
 m["Other"]=json::array({FD("notes","Anything the organizer should know?","textarea",false)});return m;}();return T;}
// Organizer-defined extra questions: sanitised and given server-generated keys (x1, x2, ...)
string normalizeCustom(const string&raw){json in=raw.empty()?json::array():json::parse(raw,nullptr,false);if(in.is_discarded()||!in.is_array())throw HttpErr{400,"Invalid custom questions"};
 if(in.size()>8)throw HttpErr{400,"At most 8 custom questions are allowed"};json out=json::array();int n=0;
 for(auto&q:in){if(!q.is_object())continue;string l=q.value("label",string()),t=q.value("type",string("text"));while(!l.empty()&&l.back()==' ')l.pop_back();if(l.empty())continue;
  if(l.size()>80)throw HttpErr{400,"Custom question text is too long"};
  if(t!="text"&&t!="textarea"&&t!="select"&&t!="number"&&t!="tel"&&t!="checkbox")throw HttpErr{400,"Invalid custom question type"};
  json f={{"key","x"+to_string(++n)},{"label",l},{"type",t},{"required",q.value("required",false)==true}};
  if(t=="select"){json o=json::array();if(q.contains("options")&&q["options"].is_array())for(auto&x:q["options"])if(x.is_string()&&!x.get<string>().empty()&&x.get<string>().size()<=40)o.push_back(x);
   if(o.size()<2||o.size()>12)throw HttpErr{400,"Dropdown questions need 2-12 options"};f["options"]=o;}
  out.push_back(f);}return out.dump();}
// Final field list for an event: common student details + category (or chosen) template + custom questions + declaration
json formFields(const string&category,const string&formType,const string&custom){auto&T=formTemplates();string k=formType.empty()?category:formType;auto it=T.find(k);if(it==T.end())it=T.find("Other");
 json all=commonFields();for(auto&f:it->second)all.push_back(f);json c=json::parse(custom.empty()?"[]":custom,nullptr,false);if(c.is_array())for(auto&f:c)all.push_back(f);
 all.push_back(FD("declaration","I confirm that the details above are correct","checkbox",true));return all;}
// Validates a student's submitted answers against the event's form; returns [{key,label,value}] in form order
json validateAnswers(const json&fields,const json&ans){json out=json::array();
 for(auto&f:fields){string k=f["key"],lab=f["label"],t=f["type"];bool req=f.value("required",false);string v;
  if(ans.is_object()&&ans.contains(k)){auto&x=ans[k];if(x.is_string())v=x.get<string>();else if(x.is_boolean())v=x.get<bool>()?"Yes":"";else if(x.is_number())v=x.dump();}
  size_t a=v.find_first_not_of(" \t\r\n"),z=v.find_last_not_of(" \t\r\n");v=a==string::npos?"":v.substr(a,z-a+1);
  if(t=="checkbox")v=(v=="Yes"||v=="true")?"Yes":"";
  if(v.empty()){if(req)throw HttpErr{400,lab+" is required"};continue;}
  if(v.size()>(t=="textarea"?600u:150u))throw HttpErr{400,lab+" is too long"};
  if(t=="select"){bool ok=false;for(auto&o:f["options"])if(o==v)ok=true;if(!ok)throw HttpErr{400,lab+": please choose one of the listed options"};}
  else if(t=="number"){char*e=nullptr;long n=strtol(v.c_str(),&e,10);if(*e||v.size()>9)throw HttpErr{400,lab+" must be a whole number"};
   if(f.contains("max")&&(n<f["min"].get<long>()||n>f["max"].get<long>()))throw HttpErr{400,lab+" must be between "+to_string(f["min"].get<int>())+" and "+to_string(f["max"].get<int>())};v=to_string(n);}
  else if(t=="tel"){int d=0;for(char ch:v){if(isdigit((unsigned char)ch))d++;else if(!strchr("+- ()",ch))throw HttpErr{400,lab+" must be a valid phone number"};}if(d<7||d>15)throw HttpErr{400,lab+" must be a valid phone number"};}
  out.push_back({{"key",k},{"label",lab},{"value",v}});}
 return out;}
struct Up{const string*s;size_t p=0;};
size_t rdcb(char*b,size_t sz,size_t n,void*u){Up*x=(Up*)u;size_t m=min(sz*n,x->s->size()-x->p);memcpy(b,x->s->data()+x->p,m);x->p+=m;return m;}
// Real SMTP delivery through libcurl. Returns "sent", "test_mode" (no SMTP configured) or an error text.
string sendMail(const string&to,const string&name,const string&ev,const string&pdf,const string&fname){
 string url=genv("SMTP_URL"),user=genv("SMTP_USER"),pass=genv("SMTP_PASS"),from=genv("SMTP_FROM",user.c_str());
 if(url.empty())return "test_mode";
 string bd="ES"+rnd(8);
 string msg="From: EventSphere <"+from+">\r\nTo: "+to+"\r\nSubject: Your EventSphere Participation Certificate\r\nMIME-Version: 1.0\r\nContent-Type: multipart/mixed; boundary=\""+bd+"\"\r\n\r\n--"+bd+"\r\nContent-Type: text/plain; charset=utf-8\r\n\r\nHi "+name+",\r\n\r\nThank you for attending "+ev+". Your participation certificate is attached.\r\n\r\nRegards,\r\nEventSphere\r\n\r\n--"+bd+"\r\nContent-Type: application/pdf; name=\""+fname+"\"\r\nContent-Transfer-Encoding: base64\r\nContent-Disposition: attachment; filename=\""+fname+"\"\r\n\r\n"+b64(pdf)+"\r\n--"+bd+"--\r\n";
 CURL*c=curl_easy_init();if(!c)return "curl init failed";Up up{&msg};
 curl_slist*rc=curl_slist_append(nullptr,("<"+to+">").c_str());string mf="<"+from+">";
 curl_easy_setopt(c,CURLOPT_URL,url.c_str());curl_easy_setopt(c,CURLOPT_USERNAME,user.c_str());curl_easy_setopt(c,CURLOPT_PASSWORD,pass.c_str());
 curl_easy_setopt(c,CURLOPT_MAIL_FROM,mf.c_str());curl_easy_setopt(c,CURLOPT_MAIL_RCPT,rc);curl_easy_setopt(c,CURLOPT_USE_SSL,(long)CURLUSESSL_ALL);
 curl_easy_setopt(c,CURLOPT_READFUNCTION,rdcb);curl_easy_setopt(c,CURLOPT_READDATA,&up);curl_easy_setopt(c,CURLOPT_UPLOAD,1L);curl_easy_setopt(c,CURLOPT_TIMEOUT,40L);
 CURLcode r=curl_easy_perform(c);curl_slist_free_all(rc);curl_easy_cleanup(c);
 return r==CURLE_OK?"sent":string(curl_easy_strerror(r));}
int main(){
 auto env=[](const char*k,const char*d){const char*v=getenv(k);return string(v?v:d);};
 Db db(env("DB_PATH","database/eventsphere.db"));
 {ifstream f("database/schema.sql");stringstream ss;ss<<f.rdbuf();db.exec(ss.str());}
 try{db.exec("ALTER TABLE registrations ADD COLUMN qr_token TEXT");}catch(...){}
 // upgrade databases created by earlier versions (each ALTER simply fails if the column already exists)
 for(const char*m:{"ALTER TABLE events ADD COLUMN form_type TEXT NOT NULL DEFAULT ''","ALTER TABLE events ADD COLUMN form_fields TEXT NOT NULL DEFAULT '[]'",
  "ALTER TABLE registrations ADD COLUMN answers TEXT NOT NULL DEFAULT '[]'","ALTER TABLE certificates ADD COLUMN signed INTEGER NOT NULL DEFAULT 0"})try{db.exec(m);}catch(...){}
 // organizers that existed before the approval workflow are grandfathered in as approved
 db.exec("INSERT INTO organizer_requests(user_id,event_title,status,admin_note,reviewed_at) SELECT id,'Existing organizer account','approved','Approved before the request workflow existed',datetime('now') FROM users WHERE role='organizer' AND id NOT IN (SELECT user_id FROM organizer_requests)");
 db.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_reg_qr ON registrations(qr_token)");
 for(auto&r:db.q("SELECT id FROM registrations WHERE qr_token IS NULL"))db.q("UPDATE registrations SET qr_token=? WHERE id=?",{rnd(16),to_string(r["id"].get<long long>())});
 string ae=env("ADMIN_EMAIL",""),ap=env("ADMIN_PASSWORD","");
 if(!ae.empty()&&ap.size()>=8&&db.q("SELECT id FROM users WHERE email=?",{ae}).empty())
  db.q("INSERT INTO users(name,email,password_hash,role) VALUES('Administrator',?,?,'admin')",{ae,hashPw(ap,rnd(16))});
 map<string,long long> sess;mutex mu;Server svr;
 const string TODAY="date('now','+"+env("UTC_OFFSET_MIN","330")+" minutes')";
 string certDir=env("CERT_DIR","certificates");filesystem::create_directories(certDir);curl_global_init(CURL_GLOBAL_DEFAULT);
 const string secret=rnd(32);
 // short-lived (60 s window) signed token shown in the organizer's live event QR
 auto ctok=[&](const string&eid,long long w){string d=eid+":"+to_string(w);unsigned char o[32];unsigned n=0;HMAC(EVP_sha256(),secret.data(),secret.size(),(const unsigned char*)d.data(),d.size(),o,&n);return hex(o,10);};
 // ---- in-app notifications ----
 auto notify=[&](const string&uid,const string&m){db.q("INSERT INTO notifications(user_id,message) VALUES(?,?)",{uid,m});};
 auto notifyRole=[&](const string&role,const string&m,const string&except){db.q("INSERT INTO notifications(user_id,message) SELECT id,? FROM users WHERE role=? AND id<>?",{m,role,except});};
 // ---- certificate email runs in a background thread so scanning stays instant ----
 auto deliver=[&](string cid,string to,string name,string ev,string pdf){thread([&db,&mu,cid,to,name,ev,pdf]{string st=sendMail(to,name,ev,pdf,cid+".pdf"),err;
  if(st!="sent"&&st!="test_mode"){err=st;st="failed";}lock_guard<mutex> l(mu);try{db.q("UPDATE certificates SET email_status=?,email_error=? WHERE cert_id=?",{st,err,cid});}catch(...){}}).detach();};
 auto auth=[&](const Request&q)->unique_ptr<User>{string c=q.get_header_value("Cookie");auto p=c.find("sid=");if(p==string::npos)return nullptr;
  auto it=sess.find(c.substr(p+4,64));if(it==sess.end())return nullptr;
  auto r=db.q("SELECT id,name,email,role FROM users WHERE id=?",{to_string(it->second)});return r.empty()?nullptr:makeUser(r[0]);};
 auto need=[&](const Request&q,function<bool(const User&)> ok){auto u=auth(q);if(!u)throw HttpErr{401,"Please log in"};if(!ok(*u))throw HttpErr{403,"You are not allowed to do this"};return u;};
 auto route=[&](bool post,const char*p,function<json(const Request&,json&)> fn){
  auto h=[&,fn](const Request&q,Response&r){lock_guard<mutex> l(mu);
   auto fail=[&](int c,const string&m){try{db.exec("ROLLBACK");}catch(...){}r.status=c;r.set_content(json{{"error",m}}.dump(),"application/json");};
   try{json b=json::object();if(!q.body.empty()){b=json::parse(q.body,nullptr,false);if(b.is_discarded())throw HttpErr{400,"Invalid JSON"};}
    json o=fn(q,b);if(o.contains("_raw")){r.status=200;r.set_header("Content-Disposition",o["_name"].get<string>());r.set_content(o["_raw"].get<string>(),"application/pdf");return;}r.status=o.value("_status",200);if(o.contains("_cookie"))r.set_header("Set-Cookie",o["_cookie"].get<string>());
    o.erase("_status");o.erase("_cookie");r.set_content(o.dump(),"application/json");}
   catch(HttpErr&x){fail(x.code,x.msg);} catch(exception&x){cerr<<"error: "<<x.what()<<endl;fail(500,"Server error");}};
  if(post)svr.Post(p,h);else svr.Get(p,h);};
 auto str=[](json&b,const char*k){return b.contains(k)&&b[k].is_string()?b[k].get<string>():string();};
 const string EV="SELECT e.*,u.name organizer,(SELECT COUNT(*) FROM registrations r WHERE r.event_id=e.id AND r.status='confirmed') taken FROM events e JOIN users u ON u.id=e.organizer_id ";
 // ---- auth ----
 route(true,"/api/signup",[&](const Request&,json&b){string n=str(b,"name"),e=str(b,"email"),p=str(b,"password"),r="student"; // role from the client is ignored: organizer status is granted only by an admin
  if(n.empty()||e.find('@')==string::npos||e.find_first_of(" \r\n<>")!=string::npos)throw HttpErr{400,"Name and a valid email are required"};
  if(p.size()<8)throw HttpErr{400,"Password must be at least 8 characters"};
  if(!db.q("SELECT id FROM users WHERE email=?",{e}).empty())throw HttpErr{409,"Email already registered"};
  db.q("INSERT INTO users(name,email,password_hash,role) VALUES(?,?,?,?)",{n,e,hashPw(p,rnd(16)),r});return json{{"_status",201},{"message","Account created"}};});
 route(true,"/api/login",[&](const Request&,json&b){auto r=db.q("SELECT * FROM users WHERE email=?",{str(b,"email")});
  if(r.empty()||!checkPw(str(b,"password"),r[0]["password_hash"]))throw HttpErr{401,"Invalid email or password"};
  string t=rnd(32);sess[t]=r[0]["id"];return json{{"_cookie","sid="+t+"; Path=/; HttpOnly; SameSite=Strict"},{"user",{{"name",r[0]["name"]},{"role",r[0]["role"]}}}};});
 route(true,"/api/logout",[&](const Request&q,json&){string c=q.get_header_value("Cookie");auto p=c.find("sid=");if(p!=string::npos)sess.erase(c.substr(p+4,64));
  return json{{"_cookie","sid=; Path=/; Max-Age=0"},{"ok",true}};});
 route(false,"/api/me",[&](const Request&q,json&){auto u=auth(q);if(!u)return json{{"user",nullptr}};return json{{"user",{{"name",u->name},{"email",u->email},{"role",u->role}}}};});
 // ---- events ----
 static const set<string> cats={"Technical","Workshop","Seminar","Cultural","Sports","Hackathon","Symposium","Other"};
 auto fields=[&](json&b){vector<string> v;
  for(auto k:{"title","description","category","banner","venue","event_date","start_time","end_time","reg_open","reg_close","capacity","eligibility","contact","form_type","form_fields"})
   v.push_back(b.contains(k)?(b[k].is_string()?b[k].get<string>():b[k].dump()):"");
  if(v[0].empty()||v[4].empty())throw HttpErr{400,"Title and venue are required"};
  if(!cats.count(v[2]))throw HttpErr{400,"Invalid category"};
  for(int i:{5,8,9})if(v[i].size()!=10)throw HttpErr{400,"Dates must be YYYY-MM-DD"};
  if(v[8]>v[9]||v[9]>v[5])throw HttpErr{400,"Registration window must open before it closes, and close on or before the event date"};
  int cap=atoi(v[10].c_str());if(cap<1||cap>100000)throw HttpErr{400,"Capacity must be 1-100000"};v[10]=to_string(cap);
  if(!v[13].empty()&&!formTemplates().count(v[13]))throw HttpErr{400,"Invalid registration form type"};v[14]=normalizeCustom(v[14]);return v;};
 route(false,"/api/events",[&](const Request&q,json&){string sql=EV+"WHERE e.status='published'";vector<string> p;
  if(q.has_param("category")&&!q.get_param_value("category").empty()){sql+=" AND e.category=?";p.push_back(q.get_param_value("category"));}
  if(q.has_param("q")&&!q.get_param_value("q").empty()){sql+=" AND (e.title LIKE ? OR e.description LIKE ?)";string s="%"+q.get_param_value("q")+"%";p.push_back(s);p.push_back(s);}
  return json{{"events",db.q(sql+" ORDER BY e.event_date",p)}};});
 route(false,"/api/form-templates",[&](const Request&,json&){json t=json::object();for(auto&kv:formTemplates())t[kv.first]=kv.second;return json{{"common",commonFields()},{"templates",t}};});
 // one event + the registration form that belongs to it. Unpublished events are visible only to their organizer and admins.
 route(false,R"(/api/events/(\d+))",[&](const Request&q,json&){string id=q.matches[1];auto e=db.q(EV+"WHERE e.id=?",{id});if(e.empty())throw HttpErr{404,"Event not found"};
  auto u=auth(q);if(e[0]["status"]!="published"&&(!u||(u->role!="admin"&&u->id!=e[0]["organizer_id"].get<long long>())))throw HttpErr{404,"Event not found"};
  json o=e[0];o["open"]=db.q("SELECT reg_open<="+TODAY+" AND reg_close>="+TODAY+" open FROM events WHERE id=?",{id})[0]["open"];
  o["form"]=formFields(o["category"],o["form_type"].is_null()?"":o["form_type"].get<string>(),o["form_fields"].is_null()?"[]":o["form_fields"].get<string>());
  o["my_registration"]=nullptr;if(u){auto r=db.q("SELECT reg_code,status FROM registrations WHERE event_id=? AND student_id=?",{id,to_string(u->id)});if(!r.empty())o["my_registration"]=r[0];}
  return json{{"event",o}};});
 route(true,"/api/events",[&](const Request&q,json&b){auto u=need(q,[](const User&x){return x.canManageEvents();});auto v=fields(b);v.insert(v.begin(),to_string(u->id));
  db.exec("BEGIN IMMEDIATE");db.q("INSERT INTO events(organizer_id,title,description,category,banner,venue,event_date,start_time,end_time,reg_open,reg_close,capacity,eligibility,contact,form_type,form_fields) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",v);
  db.q("INSERT INTO event_approval_history(event_id,actor_id,action) VALUES(?,?,'submitted')",{to_string(db.lastId()),to_string(u->id)});notifyRole("admin","New event proposal: "+v[1]+" by "+u->name,"0");db.exec("COMMIT");
  return json{{"_status",201},{"message","Proposal submitted for approval"}};});
 route(true,R"(/api/events/(\d+)/update)",[&](const Request&q,json&b){auto u=need(q,[](const User&x){return x.role=="organizer";});string id=q.matches[1];auto v=fields(b);
  if(db.q("SELECT id FROM events WHERE id=? AND organizer_id=?",{id,to_string(u->id)}).empty())throw HttpErr{404,"Event not found"};
  v.push_back(id);db.exec("BEGIN IMMEDIATE");
  db.q("UPDATE events SET title=?,description=?,category=?,banner=?,venue=?,event_date=?,start_time=?,end_time=?,reg_open=?,reg_close=?,capacity=?,eligibility=?,contact=?,form_type=?,form_fields=?,status='pending',updated_at=datetime('now') WHERE id=?",v);
  db.q("INSERT INTO event_approval_history(event_id,actor_id,action) VALUES(?,?,'resubmitted')",{id,to_string(u->id)});notifyRole("admin","Event updated for re-review: "+v[0],"0");db.exec("COMMIT");return json{{"message","Event updated and sent for re-review"}};});
 route(false,"/api/my/events",[&](const Request&q,json&){auto u=need(q,[](const User&x){return x.role=="organizer";});
  return json{{"events",db.q(EV+"WHERE e.organizer_id=? ORDER BY e.id DESC",{to_string(u->id)})}};});
 route(false,"/api/admin/events",[&](const Request&q,json&){need(q,[](const User&x){return x.canReview();});
  return json{{"events",db.q(EV+"ORDER BY (e.status='pending') DESC,e.id DESC")}};});
 route(true,R"(/api/admin/events/(\d+)/review)",[&](const Request&q,json&b){auto u=need(q,[](const User&x){return x.canReview();});string id=q.matches[1],a=str(b,"action"),why=str(b,"reason");
  static const map<string,string> st={{"approve","published"},{"reject","rejected"},{"changes","changes_requested"}};
  if(!st.count(a))throw HttpErr{400,"Action must be approve, reject or changes"};if(a!="approve"&&why.empty())throw HttpErr{400,"A reason is required"};
  auto ev=db.q("SELECT title,organizer_id,event_date FROM events WHERE id=?",{id});if(ev.empty())throw HttpErr{404,"Event not found"};
  db.exec("BEGIN IMMEDIATE");db.q("UPDATE events SET status=? WHERE id=?",{st.at(a),id});
  db.q("INSERT INTO event_approval_history(event_id,actor_id,action,reason) VALUES(?,?,?,?)",{id,to_string(u->id),a,why});{string T=ev[0]["title"],org=to_string(ev[0]["organizer_id"].get<long long>());
   notify(org,"Your event \""+T+"\" was "+(a=="approve"?string("approved and published"):a=="reject"?"rejected: "+why:"sent back for changes: "+why));
   if(a=="approve"){string m="New event: "+T+" on "+ev[0]["event_date"].get<string>();notifyRole("student",m,"0");notifyRole("organizer",m,org);}}
  db.exec("COMMIT");return json{{"message","Event "+st.at(a)}};});
 route(false,"/api/admin/stats",[&](const Request&q,json&){need(q,[](const User&x){return x.canReview();});
  return json{{"stats",db.q("SELECT (SELECT COUNT(*) FROM users) users,(SELECT COUNT(*) FROM organizer_requests WHERE status='pending') organizer_requests,(SELECT COUNT(*) FROM events WHERE status='pending') pending,(SELECT COUNT(*) FROM events WHERE status='published') published,(SELECT COUNT(*) FROM registrations WHERE status='confirmed') registrations,(SELECT COUNT(*) FROM attendance_records) attended")[0]},
   {"byCategory",db.q("SELECT e.category,COUNT(r.id) n FROM events e LEFT JOIN registrations r ON r.event_id=e.id AND r.status='confirmed' GROUP BY e.category")}};});
 // ---- registration (capacity enforced inside one IMMEDIATE transaction) ----
 route(true,R"(/api/events/(\d+)/register)",[&](const Request&q,json&b){auto u=need(q,[](const User&x){return x.canRegister();});string id=q.matches[1],uid=to_string(u->id);
  db.exec("BEGIN IMMEDIATE");
  auto e=db.q("SELECT capacity,status,organizer_id,title,category,form_type,form_fields,reg_open<="+TODAY+" AND reg_close>="+TODAY+" open FROM events WHERE id=?",{id});
  if(e.empty()||e[0]["status"]!="published")throw HttpErr{404,"Event not available"};
  if(e[0]["open"]!=1)throw HttpErr{409,"Registration is not open for this event"};
  auto ex=db.q("SELECT id,status FROM registrations WHERE event_id=? AND student_id=?",{id,uid});
  if(!ex.empty()&&ex[0]["status"]=="confirmed")throw HttpErr{409,"You are already registered"};
  if(db.q("SELECT COUNT(*) c FROM registrations WHERE event_id=? AND status='confirmed'",{id})[0]["c"].get<long long>()>=e[0]["capacity"].get<long long>())throw HttpErr{409,"This event is full"};
  // answers are validated against THIS event's form (category template + organizer's custom questions)
  string answers=validateAnswers(formFields(e[0]["category"],e[0]["form_type"].get<string>(),e[0]["form_fields"].get<string>()),b.contains("answers")?b["answers"]:json::object()).dump();
  string code="REG-"+rnd(4);for(auto&c:code)c=toupper(c);
  if(ex.empty())db.q("INSERT INTO registrations(reg_code,event_id,student_id,qr_token,answers) VALUES(?,?,?,?,?)",{code,id,uid,rnd(16),answers});
  else db.q("UPDATE registrations SET status='confirmed',reg_code=?,qr_token=?,answers=? WHERE id=?",{code,rnd(16),answers,to_string(ex[0]["id"].get<long long>())});
  notify(to_string(e[0]["organizer_id"].get<long long>()),u->name+" registered for "+e[0]["title"].get<string>());
  notify(uid,"Registration confirmed for \""+e[0]["title"].get<string>()+"\". Your registration ID is "+code+".");db.exec("COMMIT");return json{{"_status",201},{"reg_code",code},{"message","Registration confirmed"}};});
 route(true,R"(/api/registrations/(\d+)/cancel)",[&](const Request&q,json&){auto u=need(q,[](const User&x){return x.canRegister();});
  if(db.q("SELECT id FROM registrations WHERE id=? AND student_id=? AND status='confirmed'",{q.matches[1],to_string(u->id)}).empty())throw HttpErr{404,"Registration not found"};
  db.q("UPDATE registrations SET status='cancelled' WHERE id=?",{q.matches[1]});return json{{"message","Registration cancelled"}};});
 route(false,"/api/my/registrations",[&](const Request&q,json&){auto u=need(q,[](const User&x){return x.canRegister();});
  return json{{"registrations",db.q("SELECT r.id,r.event_id,r.reg_code,r.qr_token,r.status,e.title,e.venue,e.event_date,e.start_time,a.checked_in_at,c.cert_id,c.email_status,e.event_date="+TODAY+" is_today FROM registrations r JOIN events e ON e.id=r.event_id LEFT JOIN attendance_records a ON a.registration_id=r.id LEFT JOIN certificates c ON c.registration_id=r.id WHERE r.student_id=? ORDER BY e.event_date DESC",{to_string(u->id)})}};});
 auto owns=[&](const User&u,const string&eid){auto e=db.q("SELECT organizer_id FROM events WHERE id=?",{eid});if(e.empty())throw HttpErr{404,"Event not found"};if(u.role!="admin"&&e[0]["organizer_id"].get<long long>()!=u.id)throw HttpErr{403,"This is not your event"};};
 // ---- certificates: signed only with the authority configured by the admin (never a made-up signature) ----
 auto certSettings=[&]()->json{auto r=db.q("SELECT COALESCE(authority_name,'') authority_name,COALESCE(designation,'') designation,COALESCE(signature_b64,'') signature_b64 FROM certificate_settings WHERE id=1");
  return r.empty()?json{{"authority_name",""},{"designation",""},{"signature_b64",""}}:r[0];};
 auto sigReady=[&](const json&c){return !c["authority_name"].get<string>().empty()&&!c["designation"].get<string>().empty();};
 using MailJob=array<string,5>;
 // Issues + stores a signed certificate for an attended registration. Returns the certificate ID, or "" when no authority is configured yet.
 auto issueCert=[&](const string&rid,vector<MailJob>&jobs)->string{auto cs=certSettings();if(!sigReady(cs))return "";
  auto d=db.q("SELECT u.id uid,u.name,u.email,e.title,e.event_date FROM registrations r JOIN users u ON u.id=r.student_id JOIN events e ON e.id=r.event_id WHERE r.id=?",{rid})[0];
  string cid="CERT-"+rnd(5);for(auto&ch:cid)ch=toupper(ch);string issued=db.q("SELECT date('now') d")[0]["d"];
  string pdf=makePdf(d["name"],d["title"],d["event_date"],cid,env("COLLEGE_NAME","Your College Name"),issued,cs["authority_name"],cs["designation"],cs["signature_b64"]),path=certDir+"/"+cid+".pdf";{ofstream f(path,ios::binary);f<<pdf;}
  db.q("INSERT INTO certificates(cert_id,registration_id,file_path,signed) VALUES(?,?,?,1)",{cid,rid,path});
  notify(to_string(d["uid"].get<long long>()),"Attendance marked for \""+d["title"].get<string>()+"\". Your certificate is ready ("+cid+").");
  jobs.push_back({cid,d["email"],d["name"],d["title"],pdf});return cid;};
 // ---- QR attendance: token (or manual reg ID) is validated against the DB inside one transaction ----
 auto doCheck=[&](string eid,string t,string up,long long by)->json{
  if(db.q("SELECT 1 x FROM events WHERE id=? AND event_date="+TODAY,{eid}).empty())throw HttpErr{409,"Check-in is only open on the event date"};
  db.exec("BEGIN IMMEDIATE");
  auto r=db.q("SELECT r.id,r.event_id,r.status,r.reg_code,u.name FROM registrations r JOIN users u ON u.id=r.student_id WHERE r.qr_token=? OR r.reg_code=?",{t,up});
  if(r.empty())throw HttpErr{404,"Invalid QR code"};
  if(to_string(r[0]["event_id"].get<long long>())!=eid)throw HttpErr{409,"This QR is for a different event"};
  if(r[0]["status"]!="confirmed")throw HttpErr{409,"Registration was cancelled"};
  string rid=to_string(r[0]["id"].get<long long>());json o={{"name",r[0]["name"]},{"reg_code",r[0]["reg_code"]}};
  auto a=db.q("SELECT checked_in_at FROM attendance_records WHERE registration_id=?",{rid});
  bool had=!a.empty();auto cc=db.q("SELECT cert_id FROM certificates WHERE registration_id=?",{rid});
  if(had&&!cc.empty()){db.exec("COMMIT");o["result"]="already";o["time"]=a[0]["checked_in_at"];o["certificate"]=cc[0]["cert_id"];return o;}
  if(!had)db.q("INSERT INTO attendance_records(registration_id,event_id,checked_in_by) VALUES(?,?,?)",{rid,eid,to_string(by)});
  vector<MailJob> jobs;string cid=issueCert(rid,jobs);
  if(cid.empty()){auto d=db.q("SELECT u.id uid,e.title FROM registrations r JOIN users u ON u.id=r.student_id JOIN events e ON e.id=r.event_id WHERE r.id=?",{rid})[0];
   if(!had)notify(to_string(d["uid"].get<long long>()),"Attendance marked for \""+d["title"].get<string>()+"\". Your certificate will be issued as soon as the admin sets the certificate authority.");}
  db.exec("COMMIT");for(auto&j:jobs)deliver(j[0],j[1],j[2],j[3],j[4]);o["result"]=had?"already":"ok";if(cid.empty())o["certificate_pending"]=true;else o["certificate"]=cid;return o;};
 route(true,"/api/attendance/check",[&](const Request&q,json&b){auto u=need(q,[](const User&x){return x.canManageEvents();});
  string eid=b.contains("event_id")?(b["event_id"].is_string()?b["event_id"].get<string>():b["event_id"].dump()):"",t=str(b,"token"),up=t;for(auto&c:up)c=toupper(c);
  owns(*u,eid);if(t.empty())throw HttpErr{400,"Scan a QR code or enter a registration ID"};
  if(db.q("SELECT 1 x FROM events WHERE id=? AND event_date="+TODAY,{eid}).empty())throw HttpErr{409,"Check-in is only open on the event date"};
  return doCheck(eid,t,up,u->id);});
 route(false,R"(/api/events/(\d+)/participants)",[&](const Request&q,json&){auto u=need(q,[](const User&x){return x.canManageEvents();});string eid=q.matches[1];owns(*u,eid);
  return json{{"participants",db.q("SELECT u.name,u.email,r.id,r.student_id,r.answers,r.created_at,r.reg_code,r.status,a.checked_in_at,c.cert_id,c.email_status FROM registrations r JOIN users u ON u.id=r.student_id LEFT JOIN attendance_records a ON a.registration_id=r.id LEFT JOIN certificates c ON c.registration_id=r.id WHERE r.event_id=? ORDER BY u.name",{eid})}};});
 route(false,"/api/notifications",[&](const Request&q,json&){auto u=need(q,[](const User&){return true;});string id=to_string(u->id);
  return json{{"notifications",db.q("SELECT id,message,is_read,created_at FROM notifications WHERE user_id=? ORDER BY id DESC LIMIT 30",{id})},{"unread",db.q("SELECT COUNT(*) c FROM notifications WHERE user_id=? AND is_read=0",{id})[0]["c"]}};});
 route(true,"/api/notifications/read",[&](const Request&q,json&){auto u=need(q,[](const User&){return true;});db.q("UPDATE notifications SET is_read=1 WHERE user_id=?",{to_string(u->id)});return json{{"ok",true}};});
 route(false,"/api/certificates/verify/(.+)",[&](const Request&q,json&){string id=q.matches[1];for(auto&ch:id)ch=toupper(ch);
  auto c=db.q("SELECT c.issued_at,u.name,e.title,e.event_date FROM certificates c JOIN registrations r ON r.id=c.registration_id JOIN users u ON u.id=r.student_id JOIN events e ON e.id=r.event_id WHERE c.cert_id=?",{id});
  if(c.empty())throw HttpErr{404,"No certificate found with this ID"};
  return json{{"valid",true},{"name",c[0]["name"]},{"event",c[0]["title"]},{"date",c[0]["event_date"]},{"issued",c[0]["issued_at"]}};});
 route(false,R"(/api/certificates/([A-Za-z0-9-]+)/download)",[&](const Request&q,json&){auto u=need(q,[](const User&){return true;});string cid=q.matches[1];
  auto c=db.q("SELECT c.file_path,r.student_id,e.organizer_id FROM certificates c JOIN registrations r ON r.id=c.registration_id JOIN events e ON e.id=r.event_id WHERE c.cert_id=?",{cid});
  if(c.empty())throw HttpErr{404,"Certificate not found"};
  long long s=c[0]["student_id"],o=c[0]["organizer_id"];if(u->id!=s&&u->id!=o&&u->role!="admin")throw HttpErr{403,"Not allowed"};
  ifstream f(c[0]["file_path"].get<string>(),ios::binary);stringstream ss;ss<<f.rdbuf();
  return json{{"_raw",ss.str()},{"_name","attachment; filename=\""+cid+".pdf\""}};});
 route(true,R"(/api/certificates/([A-Za-z0-9-]+)/resend)",[&](const Request&q,json&){auto u=need(q,[](const User&x){return x.canManageEvents();});string cid=q.matches[1];
  auto c=db.q("SELECT c.file_path,e.id eid,e.title,u.name,u.email FROM certificates c JOIN registrations r ON r.id=c.registration_id JOIN events e ON e.id=r.event_id JOIN users u ON u.id=r.student_id WHERE c.cert_id=?",{cid});
  if(c.empty())throw HttpErr{404,"Certificate not found"};owns(*u,to_string(c[0]["eid"].get<long long>()));
  ifstream f(c[0]["file_path"].get<string>(),ios::binary);stringstream ss;ss<<f.rdbuf();
  db.q("UPDATE certificates SET email_status='pending',email_error=NULL WHERE cert_id=?",{cid});
  deliver(cid,c[0]["email"],c[0]["name"],c[0]["title"],ss.str());return json{{"message","Email retry started"}};});

 // ---- organizer approval workflow: student requests -> admin approves/rejects -> only then role becomes organizer ----
 auto trim=[](string v){size_t a=v.find_first_not_of(" \t\r\n"),z=v.find_last_not_of(" \t\r\n");return a==string::npos?string():v.substr(a,z-a+1);};
 route(true,"/api/organizer/request",[&](const Request&q,json&b){auto u=need(q,[](const User&){return true;});
  if(u->role=="organizer")throw HttpErr{409,"You are already an approved event organizer"};if(u->role!="student")throw HttpErr{403,"Only students can request organizer access"};
  string org=trim(str(b,"organization")),title=trim(str(b,"event_title")),cat=str(b,"event_category"),desc=trim(str(b,"event_description")),date=str(b,"expected_date"),why=trim(str(b,"reason"));
  if(org.empty()||title.empty()||desc.empty()||why.empty())throw HttpErr{400,"Organization, event title, event description and reason are required"};
  if(!cats.count(cat))throw HttpErr{400,"Invalid category"};if(!date.empty()&&date.size()!=10)throw HttpErr{400,"Date must be YYYY-MM-DD"};
  if(org.size()>120||title.size()>150||desc.size()>1000||why.size()>600)throw HttpErr{400,"One of the fields is too long"};
  db.exec("BEGIN IMMEDIATE");
  if(!db.q("SELECT id FROM organizer_requests WHERE user_id=? AND status='pending'",{to_string(u->id)}).empty())throw HttpErr{409,"You already have a request waiting for admin approval"};
  db.q("INSERT INTO organizer_requests(user_id,organization,event_title,event_category,event_description,expected_date,reason) VALUES(?,?,?,?,?,?,?)",{to_string(u->id),org,title,cat,desc,date,why});
  notifyRole("admin","New event organizer request from "+u->name+": "+title,"0");db.exec("COMMIT");
  return json{{"_status",201},{"message","Request submitted. You will get organizer access after the admin approves it."}};});
 route(false,"/api/organizer/request",[&](const Request&q,json&){auto u=need(q,[](const User&){return true;});
  auto r=db.q("SELECT id,organization,event_title,event_category,event_description,expected_date,reason,status,admin_note,reviewed_at,created_at FROM organizer_requests WHERE user_id=? ORDER BY id DESC LIMIT 1",{to_string(u->id)});
  return json{{"role",u->role},{"request",r.empty()?json(nullptr):r[0]}};});
 route(false,"/api/admin/organizer-requests",[&](const Request&q,json&){need(q,[](const User&x){return x.canReview();});
  return json{{"requests",db.q("SELECT q.id,q.user_id,u.name,u.email,u.role,q.organization,q.event_title,q.event_category,q.event_description,q.expected_date,q.reason,q.status,q.admin_note,q.reviewed_at,q.created_at FROM organizer_requests q JOIN users u ON u.id=q.user_id ORDER BY (q.status='pending') DESC,q.id DESC")}};});
 route(true,R"(/api/admin/organizer-requests/(\d+)/review)",[&](const Request&q,json&b){auto u=need(q,[](const User&x){return x.canReview();});string id=q.matches[1],a=str(b,"action"),why=trim(str(b,"reason"));
  if(a!="approve"&&a!="reject")throw HttpErr{400,"Action must be approve or reject"};if(a=="reject"&&why.empty())throw HttpErr{400,"A reason is required when rejecting"};
  db.exec("BEGIN IMMEDIATE");auto r=db.q("SELECT user_id,event_title,status FROM organizer_requests WHERE id=?",{id});if(r.empty())throw HttpErr{404,"Request not found"};
  if(r[0]["status"]!="pending")throw HttpErr{409,"This request was already reviewed"};string uid=to_string(r[0]["user_id"].get<long long>()),T=r[0]["event_title"];
  db.q("UPDATE organizer_requests SET status=?,admin_note=?,reviewed_by=?,reviewed_at=datetime('now') WHERE id=?",{a=="approve"?"approved":"rejected",why,to_string(u->id),id});
  if(a=="approve"){db.q("UPDATE users SET role='organizer' WHERE id=? AND role='student'",{uid});notify(uid,"Your event organizer request was approved. You can now use the Organizer Dashboard to create and manage events.");}
  else notify(uid,"Your event organizer request (\""+T+"\") was rejected: "+why);
  db.exec("COMMIT");return json{{"message",a=="approve"?"Approved: the user is now an event organizer":"Request rejected"}};});
 route(false,"/api/admin/organizers",[&](const Request&q,json&){need(q,[](const User&x){return x.canReview();});
  return json{{"organizers",db.q("SELECT u.id,u.name,u.email,u.created_at,(SELECT COUNT(*) FROM events e WHERE e.organizer_id=u.id) events,(SELECT COUNT(*) FROM registrations r JOIN events e ON e.id=r.event_id WHERE e.organizer_id=u.id AND r.status='confirmed') registrations FROM users u WHERE u.role='organizer' ORDER BY u.name")}};});
 route(true,R"(/api/admin/organizers/(\d+)/revoke)",[&](const Request&q,json&b){auto u=need(q,[](const User&x){return x.canReview();});string id=q.matches[1],why=trim(str(b,"reason"));
  db.exec("BEGIN IMMEDIATE");if(db.q("SELECT id FROM users WHERE id=? AND role='organizer'",{id}).empty())throw HttpErr{404,"Organizer not found"};
  db.q("UPDATE users SET role='student' WHERE id=?",{id});
  db.q("UPDATE organizer_requests SET status='revoked',admin_note=?,reviewed_by=?,reviewed_at=datetime('now') WHERE id=(SELECT MAX(id) FROM organizer_requests WHERE user_id=? AND status='approved')",{why,to_string(u->id),id});
  notify(id,"Your event organizer access was removed by the admin."+(why.empty()?string():" Reason: "+why));db.exec("COMMIT");return json{{"message","Organizer access removed"}};});
 route(false,"/api/admin/registrations",[&](const Request&q,json&){need(q,[](const User&x){return x.canReview();});
  return json{{"registrations",db.q("SELECT r.id,r.reg_code,r.status,r.answers,r.created_at,e.id event_id,e.title event,u.name,u.email,a.checked_in_at,c.cert_id FROM registrations r JOIN events e ON e.id=r.event_id JOIN users u ON u.id=r.student_id LEFT JOIN attendance_records a ON a.registration_id=r.id LEFT JOIN certificates c ON c.registration_id=r.id ORDER BY r.id DESC LIMIT 1000")}};});
 // ---- certificate authority (name, designation, optional signature image) configured by the admin ----
 route(false,"/api/admin/certificate-settings",[&](const Request&q,json&){need(q,[](const User&x){return x.canReview();});auto c=certSettings();string sg=c["signature_b64"];
  return json{{"authority_name",c["authority_name"]},{"designation",c["designation"]},{"signature",sg.empty()?string():"data:image/jpeg;base64,"+sg},{"configured",sigReady(c)}};});
 route(true,"/api/admin/certificate-settings",[&](const Request&q,json&b){auto u=need(q,[](const User&x){return x.canReview();});
  string name=trim(str(b,"authority_name")),des=trim(str(b,"designation")),sig=str(b,"signature");bool clear=b.contains("clear_signature")&&b["clear_signature"]==true;
  if(name.empty()||des.empty())throw HttpErr{400,"Authority name and designation are required"};
  if(name.size()>60||des.size()>80)throw HttpErr{400,"Name or designation is too long"};
  for(char ch:name+des)if((unsigned char)ch<32||(unsigned char)ch>126)throw HttpErr{400,"Use plain English letters and numbers in the name and designation"};
  auto old=certSettings();string b64sig=old["signature_b64"];if(clear)b64sig="";
  if(!sig.empty()){size_t k=sig.find("base64,");if(sig.rfind("data:image/jpeg",0)!=0||k==string::npos)throw HttpErr{400,"Signature must be a JPEG image"};
   b64sig=sig.substr(k+7);if(b64sig.size()>600000)throw HttpErr{400,"Signature image is too large (max about 400 KB)"};
   string raw=unb64(b64sig);int w,h,c;if(raw.empty()||!jpegInfo(raw,w,h,c))throw HttpErr{400,"The signature image could not be read"};}
  db.exec("BEGIN IMMEDIATE");
  db.q("INSERT INTO certificate_settings(id,authority_name,designation,signature_b64,updated_by,updated_at) VALUES(1,?,?,?,?,datetime('now')) ON CONFLICT(id) DO UPDATE SET authority_name=excluded.authority_name,designation=excluded.designation,signature_b64=excluded.signature_b64,updated_by=excluded.updated_by,updated_at=excluded.updated_at",{name,des,b64sig,to_string(u->id)});
  // certificates created before an authority existed carry no valid signature block: re-render them; then issue anything that was waiting
  int redone=0,issuedNow=0;vector<MailJob> jobs;
  for(auto&c:db.q("SELECT c.cert_id,c.file_path,c.issued_at,u.name,e.title,e.event_date FROM certificates c JOIN registrations r ON r.id=c.registration_id JOIN users u ON u.id=r.student_id JOIN events e ON e.id=r.event_id WHERE c.signed=0")){
   string pdf=makePdf(c["name"],c["title"],c["event_date"],c["cert_id"],env("COLLEGE_NAME","Your College Name"),c["issued_at"].get<string>().substr(0,10),name,des,b64sig);
   {ofstream f(c["file_path"].get<string>(),ios::binary);f<<pdf;}db.q("UPDATE certificates SET signed=1 WHERE cert_id=?",{c["cert_id"].get<string>()});redone++;}
  for(auto&r:db.q("SELECT a.registration_id id FROM attendance_records a JOIN registrations r ON r.id=a.registration_id LEFT JOIN certificates c ON c.registration_id=a.registration_id WHERE c.id IS NULL AND r.status='confirmed'"))
   if(!issueCert(to_string(r["id"].get<long long>()),jobs).empty())issuedNow++;
  db.exec("COMMIT");for(auto&j:jobs)deliver(j[0],j[1],j[2],j[3],j[4]);
  return json{{"message","Certificate authority saved"},{"reissued",redone},{"issued",issuedNow}};});
 route(false,"/api/public/stats",[&](const Request&,json&){return json{{"stats",db.q("SELECT (SELECT COUNT(*) FROM events WHERE status='published') events,(SELECT COUNT(*) FROM registrations WHERE status='confirmed') registrations,(SELECT COUNT(*) FROM certificates) certificates")[0]}};});
 route(false,R"(/api/events/(\d+)/checkin-qr)",[&](const Request&q,json&){auto u=need(q,[](const User&x){return x.canManageEvents();});string eid=q.matches[1];owns(*u,eid);
  auto ev=db.q("SELECT title,event_date,event_date="+TODAY+" is_today FROM events WHERE id=?",{eid});
  return json{{"token",ctok(eid,(long long)time(nullptr)/300)},{"title",ev[0]["title"]},{"event_date",ev[0]["event_date"]},{"is_today",ev[0]["is_today"]}};});
 route(true,"/api/attendance/self",[&](const Request&q,json&b){auto u=need(q,[](const User&x){return x.canRegister();});
  string eid=b.contains("event_id")?(b["event_id"].is_string()?b["event_id"].get<string>():b["event_id"].dump()):"",t=str(b,"token");long long w=(long long)time(nullptr)/300;
  if(t.empty()||(ctok(eid,w)!=t&&ctok(eid,w-1)!=t))throw HttpErr{400,"This QR has expired. Scan the organizer's current QR again"};
  auto r=db.q("SELECT qr_token FROM registrations WHERE event_id=? AND student_id=? AND status='confirmed'",{eid,to_string(u->id)});
  if(r.empty())throw HttpErr{409,"You are not registered for this event"};
  string tok=r[0]["qr_token"];return doCheck(eid,tok,tok,u->id);});
 svr.set_mount_point("/",env("FRONTEND_DIR","frontend"));
 int port=stoi(env("PORT","8080"));cout<<"EventSphere running on port "<<port<<endl;svr.listen("0.0.0.0",port);}
