// Integrated lock-on experiment for Half Sword 0.6.15.
// Included inside bridge_v1_3_beta.cpp after the bridge's Unreal helper types.
namespace lockon13beta {
constexpr std::uintptr_t kRootComponent=0x1A0, kRelativeLocation=0x128;
constexpr std::uintptr_t kLockCameraAt=0x1D18, kLockOnFound=0x1D30;
constexpr std::uintptr_t kCameraLocked=0x2000, kLockOnStrength=0x4680;
constexpr std::uintptr_t kHealth=0x13A8, kDead=0x13B0;
constexpr std::uintptr_t kFallen=0x13B1, kDowned=0x1C20;
constexpr std::uintptr_t kCurrentControlRotation=0x2088;
constexpr std::uintptr_t kController=0x2C8, kControllerRotation=0x308;
constexpr std::uintptr_t kBodyRotation=0x1310;
// Blueprint axis events found by resolve_player() in the bridge:
// 8 is "Turn Right / Left Mouse" and 9 is "Look Up / Down Mouse". Feeding a
// small look-axis value through the pawn's own input event is the only way
// this build can move the camera vertically while locked on.
constexpr int kLookUpDownAxisFunction=9;
constexpr double kPitchDeadzoneDegrees=1.2;
constexpr double kPitchMaxDegreesPerSecond=75.0;
constexpr double kPitchAxisUnitsPerDegree=4.0;
constexpr double kPitchAxisMaximum=60.0;
struct Vector { double x,y,z; };
struct Rotator { double pitch,yaw,roll; };
struct Target { void *actor{}; unsigned id{}; };
bool enabled[2]{true,true}, arm[2]{}, previous_arm[2]{};
void *pawn[2]{}, *target[2]{};
ULONGLONG last_turn[2]{}, last_turn_log[2]{};
LONGLONG last_turn_clock[2]{};
ULONGLONG cycle_boost_until[2]{};
unsigned turn_count[2]{};
unsigned turn_applied_count[2]{};
unsigned pitch_applied_count[2]{};
double max_turn_gap_ms[2]{};
unsigned pitch_warning_count{};
void *set_control_rotation_function{}, *failed_controller_type{};
Target candidates[128]{};
int candidate_count{}, scan_level{}, scan_actor{};
unsigned next_id{1}, generation{1};
void *known_world{};
ULONGLONG scan_pause{}, last_diag{};

LONGLONG clock_frequency(){
    static const LONGLONG frequency=[](){LARGE_INTEGER value{};return QueryPerformanceFrequency(&value)
        ?value.QuadPart:1000000LL;}();
    return frequency;
}
LONGLONG clock_now(){LARGE_INTEGER value{};QueryPerformanceCounter(&value);return value.QuadPart;}
unsigned cadence_ms(){
    const LONG value=g_settings?InterlockedCompareExchange(&g_settings->lockon_cadence_ms,0,0):1;
    return value>=1&&value<=16?static_cast<unsigned>(value):1;
}
LONGLONG cadence_ticks(unsigned interval){
    const LONGLONG ticks=(clock_frequency()*interval+999)/1000;
    return ticks>0?ticks:1;
}

bool writable(const void *p,size_t bytes){
    MEMORY_BASIC_INFORMATION i{}; if(!p||!VirtualQuery(p,&i,sizeof(i))) return false;
    auto s=reinterpret_cast<std::uintptr_t>(p),e=reinterpret_cast<std::uintptr_t>(i.BaseAddress)+i.RegionSize;
    const DWORD ok=PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY;
    return i.State==MEM_COMMIT&&!(i.Protect&(PAGE_NOACCESS|PAGE_GUARD))&&(i.Protect&ok)&&
        s>=reinterpret_cast<std::uintptr_t>(i.BaseAddress)&&bytes<=e-s;
}
bool name_of(void *o,wchar_t *b,int cap){
    if(!readable(o,sizeof(Object))) return false;
    auto f=reinterpret_cast<AppendName>(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))+kAppendName);
    if(!executable(reinterpret_cast<void*>(f))) return false;
    String s{b,0,cap}; f(&static_cast<Object*>(o)->name,s); return s.count>0;
}
bool is_fighter(void *o){
    if(!readable(o,sizeof(Object))) return false; void *k=static_cast<Object*>(o)->type;
    for(int d=0;k&&d<12;++d){ wchar_t n[128]{}; if(name_of(k,n,128)&&wcscmp(n,L"Willie_BP_C")==0)return true;
        auto sup=reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(k)+0x40);
        if(!readable(sup,sizeof(void*)))break; k=*sup; }
    return false;
}
bool location(void *a,Vector &v){
    if(!readable(a,kRootComponent+sizeof(void*)))return false;
    void *r=*reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(a)+kRootComponent);
    auto p=reinterpret_cast<Vector*>(reinterpret_cast<std::uintptr_t>(r)+kRelativeLocation);
    if(!readable(p,sizeof(Vector)))return false; v=*p;
    return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&
        std::fabs(v.x)<1e7&&std::fabs(v.y)<1e7&&std::fabs(v.z)<1e7;
}
bool alive(void *a){
    auto b=reinterpret_cast<std::uintptr_t>(a); if(!a||!readable(reinterpret_cast<void*>(b+kDead),1))return false;
    auto h=reinterpret_cast<double*>(b+kHealth);
    return *reinterpret_cast<unsigned char*>(b+kDead)==0&&
        *reinterpret_cast<unsigned char*>(b+kFallen)==0&&
        readable(reinterpret_cast<void*>(b+kDowned),1)&&
        *reinterpret_cast<unsigned char*>(b+kDowned)==0&&
        readable(h,sizeof(double))&&std::isfinite(*h)&&*h>0.0;
}
void clear_applied(int i){
    void *p=pawn[i]; if(p&&writable(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(p)+kLockOnFound),1)){
        *reinterpret_cast<unsigned char*>(reinterpret_cast<std::uintptr_t>(p)+kLockOnFound)=0;
        *reinterpret_cast<unsigned char*>(reinterpret_cast<std::uintptr_t>(p)+kCameraLocked)=0; }
}
bool resolve_control_rotation(void *controller){
    if(set_control_rotation_function)return true;
    if(!readable(controller,sizeof(Object)))return false;
    void *type=static_cast<Object*>(controller)->type;
    if(type==failed_controller_type)return false;
    for(int depth=0;type&&depth<16;++depth){
        wchar_t class_name[128]{};
        if(name_of(type,class_name,128)&&wcscmp(class_name,L"Controller")==0){
            auto children=reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(type)+0x48);
            if(!readable(children,sizeof(void*)))break;
            void *child=*children;
            for(int count=0;child&&count<2000;++count){
                if(!readable(child,sizeof(Object)))break;
                wchar_t function_name[128]{};
                if(name_of(child,function_name,128)&&wcscmp(function_name,L"SetControlRotation")==0){
                    set_control_rotation_function=child;
                    std::fprintf(g_log,"LOCK yaw-only SetControlRotation=%p\n",child);std::fflush(g_log);
                    return true;
                }
                auto next=reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(child)+0x28);
                if(!readable(next,sizeof(void*)))break;
                child=*next;
            }
            break;
        }
        auto super=reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(type)+0x40);
        if(!readable(super,sizeof(void*)))break;
        type=*super;
    }
    failed_controller_type=static_cast<Object*>(controller)->type;
    std::fprintf(g_log,"LOCK yaw-only SetControlRotation unavailable type=%p\n",failed_controller_type);
    std::fflush(g_log);
    return false;
}
// Feed a small value into the pawn's own "Look Up / Down Mouse" axis event so
// the game rotates the camera vertically by itself. This is the same path the
// joiner's mouse uses, so it respects the game's own pitch limits and camera
// code instead of fighting them. The look axis convention matches the bridge:
// moving the remote mouse down (positive delta) produces a positive axis value
// and looks down (more negative pitch), so aiming below the horizon needs a
// positive axis step. Returns false when the blueprint function is missing.
bool apply_pitch_axis(void *pawn_actor,const Vector &offset,double seconds,int slot){
    const double horizontal=std::sqrt(offset.x*offset.x+offset.y*offset.y);
    if(!(horizontal>1.0))return true; // directly above or below: nothing to aim at
    const double wanted_pitch=-std::atan2(offset.z,horizontal)*180.0/3.141592653589793;
    // The pawn's current view pitch is only meaningful for the locally
    // controlled joiner fighter (slot 1). Player 1's camera is steered by the
    // native game lock-on, so its own pitch must not be overwritten here.
    if(slot!=1)return true;
    auto view=reinterpret_cast<Rotator*>(reinterpret_cast<std::uintptr_t>(pawn_actor)+kCurrentControlRotation);
    if(!readable(view,sizeof(Rotator)))return true;
    const double current_pitch=view->pitch;
    if(!std::isfinite(current_pitch)||!std::isfinite(wanted_pitch))return false;
    const double error=wanted_pitch-current_pitch;
    if(std::fabs(error)<kPitchDeadzoneDegrees)return true;
    double step=error;
    const double max_step=kPitchMaxDegreesPerSecond*seconds;
    if(step>max_step)step=max_step;
    if(step<-max_step)step=-max_step;
    // Axis sign follows the game's mouse-look convention: looking down (more
    // negative pitch) means a positive axis value.
    double axis=-step*kPitchAxisUnitsPerDegree;
    if(axis>kPitchAxisMaximum)axis=kPitchAxisMaximum;
    if(axis<-kPitchAxisMaximum)axis=-kPitchAxisMaximum;
    if(axis==0.0)return true;
    void *look_function=g_functions[kLookUpDownAxisFunction];
    if(!look_function||!readable(look_function,sizeof(Object)))return false;
    auto table=*reinterpret_cast<void***>(pawn_actor);
    if(!readable(table,(kProcessEventSlot+1)*sizeof(void*)))return false;
    auto process=reinterpret_cast<ProcessEvent>(table[kProcessEventSlot]);
    if(!executable(reinterpret_cast<void*>(process)))return false;
    const float axis_value=static_cast<float>(axis);
    __try{ process(pawn_actor,look_function,&axis_value); }
    __except(EXCEPTION_EXECUTE_HANDLER){ return false; }
    if(slot>=0&&slot<2)++pitch_applied_count[slot];
    return true;
}
void apply(int i){
    void *p=pawn[i],*t=target[i]; Vector v{}, own{};
    // The native lock flag stays untouched; both axes are steered here.
    if(!enabled[i]||!arm[i]||!p||!t||!alive(t)||!location(t,v)||!location(p,own))return;
    const double dx=v.x-own.x,dy=v.y-own.y,dz=v.z-own.z;
    const double distance_squared=dx*dx+dy*dy;
    if(distance_squared<100.0)return;
    // Cap the amount turned after a late callback, while measuring ordinary
    // callback spacing with the high-resolution clock.
    const bool close=distance_squared<300.0*300.0;
    const unsigned interval=cadence_ms();
    const LONGLONG precise_now=clock_now();
    if(last_turn_clock[i]&&precise_now-last_turn_clock[i]<cadence_ticks(interval))return;
    const double gap_ms=last_turn_clock[i]
        ?1000.0*static_cast<double>(precise_now-last_turn_clock[i])/clock_frequency()
        :static_cast<double>(interval);
    last_turn_clock[i]=precise_now;
    const ULONGLONG now=GetTickCount64();
    // A late game-thread callback must not produce a single large yaw jump.
    // The next callbacks can close the remaining angle in ordinary steps.
    const double seconds=std::fmin(gap_ms,close?16.0:32.0)/1000.0;
    last_turn[i]=now;
    ++turn_count[i];if(gap_ms>max_turn_gap_ms[i])max_turn_gap_ms[i]=gap_ms;
    // Vertical camera correction (the previous build only ever turned yaw, so
    // a locked target above or below eye level was never centred). This runs
    // before the yaw early-return below because pitch is an independent axis.
    if(!apply_pitch_axis(p,{dx,dy,dz},seconds,i)&&pitch_warning_count++<20){
        std::fprintf(g_log,"LOCK P%d pitch axis dispatch unavailable\n",i+1);std::fflush(g_log); }
    auto controller_ptr=reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(p)+kController);
    if(!readable(controller_ptr,sizeof(void*))||!*controller_ptr)return;
    void *controller=*controller_ptr;
    auto rotation_ptr=reinterpret_cast<Rotator*>(reinterpret_cast<std::uintptr_t>(controller)+kControllerRotation);
    if(!readable(rotation_ptr,sizeof(Rotator))||!resolve_control_rotation(controller))return;
    Rotator current=*rotation_ptr;
    if(!std::isfinite(current.pitch)||!std::isfinite(current.yaw)||!std::isfinite(current.roll))return;
    const double wanted=std::atan2(dy,dx)*180.0/3.141592653589793;
    double delta=std::remainder(wanted-current.yaw,360.0);
    if(std::fabs(delta)<2.0)cycle_boost_until[i]=0;
    if(std::fabs(delta)<1.2)return;
    const bool cycling=cycle_boost_until[i]&&now<cycle_boost_until[i];
    const double speed=cycling?std::fmin(260.0,std::fabs(delta)*10.0)
        :std::fmin(75.0,std::fabs(delta)*5.0);
    const double max_step=speed*seconds;
    if(delta>max_step)delta=max_step;
    if(delta<-max_step)delta=-max_step;
    Rotator requested=current;
    requested.yaw=std::remainder(current.yaw+delta,360.0);
    auto table=*reinterpret_cast<void***>(controller);
    if(!readable(table,(kProcessEventSlot+1)*sizeof(void*)))return;
    auto process=reinterpret_cast<ProcessEvent>(table[kProcessEventSlot]);
    if(!executable(reinterpret_cast<void*>(process)))return;
    process(controller,set_control_rotation_function,&requested);
    ++turn_applied_count[i];
    const double pitch_change=std::remainder(rotation_ptr->pitch-current.pitch,360.0);
    if(std::isfinite(pitch_change)&&std::fabs(pitch_change)>0.5&&pitch_warning_count++<20){
        std::fprintf(g_log,"LOCK P%d yaw call changed pitch by %.2f degrees\n",i+1,pitch_change);
        std::fflush(g_log);
    }
    if(now-last_turn_log[i]>=2000){
        last_turn_log[i]=now;
        auto body=reinterpret_cast<Rotator*>(reinterpret_cast<std::uintptr_t>(p)+kBodyRotation);
        auto view=reinterpret_cast<Rotator*>(reinterpret_cast<std::uintptr_t>(p)+kCurrentControlRotation);
        const double wanted_pitch=-std::atan2(dz,std::sqrt(distance_squared))*180.0/3.141592653589793;
        std::fprintf(g_log,"LOCK P%d yaw controller=%.2f wanted=%.2f step=%.2f cycle_boost=%d body=%.2f view=(%.2f,%.2f) wanted_pitch=%.2f checks=%u turns=%u pitch_steps=%u max_gap_ms=%.2f distance=%.1f cadence_ms=%u\n",
            i+1,current.yaw,wanted,delta,cycling?1:0,readable(body,sizeof(Rotator))?body->yaw:9999.0,
            readable(view,sizeof(Rotator))?view->pitch:9999.0,readable(view,sizeof(Rotator))?view->yaw:9999.0,
            wanted_pitch,turn_count[i],turn_applied_count[i],pitch_applied_count[i],
            max_turn_gap_ms[i],std::sqrt(distance_squared),interval);
        turn_count[i]=turn_applied_count[i]=pitch_applied_count[i]=0;max_turn_gap_ms[i]=0;
        std::fflush(g_log);
    }
}
bool get_players(void *out[2]){
    out[0]=out[1]=nullptr; auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    auto ws=reinterpret_cast<void**>(base+kWorld); if(!readable(ws,8)||!*ws)return false;
    void *w=*ws; auto inst=reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(w)+0x1D8);
    if(!readable(inst,8)||!*inst)return false;
    auto locals=reinterpret_cast<Array*>(reinterpret_cast<std::uintptr_t>(*inst)+0x38);
    if(!readable(locals,sizeof(Array))||locals->count<1||locals->count>8||!readable(locals->data,locals->count*sizeof(void*)))return false;
    for(int i=0;i<locals->count&&i<2;++i){ void *l=static_cast<void**>(locals->data)[i];
        auto c=reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(l)+0x30);
        if(!readable(c,8)||!*c)continue; auto p=reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(*c)+0x2D0);
        if(readable(p,8)&&is_fighter(*p))out[i]=*p; }
    return out[0]||out[1];
}
void diagnostic_orientation(int i,FILE *output){
    if(!output||i<0||i>1)return;
    void *p=pawn[i];
    if(!p||!readable(p,sizeof(Object))){void *found[2]{};get_players(found);p=found[i];}
    Rotator control{},view{},body{};bool have_control=false,have_view=false,have_body=false;
    if(p&&readable(p,sizeof(Object))){
        auto controller_ptr=reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(p)+kController);
        if(readable(controller_ptr,sizeof(void*))&&*controller_ptr){
            auto rotation=reinterpret_cast<Rotator*>(reinterpret_cast<std::uintptr_t>(*controller_ptr)+kControllerRotation);
            if(readable(rotation,sizeof(Rotator))){control=*rotation;have_control=true;}
        }
        auto v=reinterpret_cast<Rotator*>(reinterpret_cast<std::uintptr_t>(p)+kCurrentControlRotation);
        auto b=reinterpret_cast<Rotator*>(reinterpret_cast<std::uintptr_t>(p)+kBodyRotation);
        if(readable(v,sizeof(Rotator))){view=*v;have_view=true;}
        if(readable(b,sizeof(Rotator))){body=*b;have_body=true;}
    }
    std::fprintf(output,"LOCK-ORIENTATION t=%llu p=%d enabled=%d arm=%d target=%p "
        "control=(%.3f,%.3f,%d) view=(%.3f,%.3f,%d) body=(%.3f,%.3f,%d)\n",
        static_cast<unsigned long long>(GetTickCount64()),i+1,enabled[i]?1:0,arm[i]?1:0,target[i],
        control.pitch,control.yaw,have_control?1:0,
        view.pitch,view.yaw,have_view?1:0,
        body.pitch,body.yaw,have_body?1:0);
}
void reset_registry(const char *why){
    for(int i=0;i<2;++i){clear_applied(i);target[i]=nullptr;pawn[i]=nullptr;arm[i]=previous_arm[i]=false;
        last_turn[i]=cycle_boost_until[i]=0;last_turn_clock[i]=0;}
    candidate_count=scan_level=scan_actor=0;next_id=1;++generation;scan_pause=0;
    std::fprintf(g_log,"LOCK generation=%u registry reset reason=%s\n",generation,why);std::fflush(g_log);
}
void remember(void *a){
    if(!a||!is_fighter(a))return; Vector v{};if(!location(a,v))return;
    for(int i=0;i<candidate_count;++i)if(candidates[i].actor==a)return;
    if(candidate_count<128){candidates[candidate_count++]={a,next_id++};
        std::fprintf(g_log,"LOCK generation=%u registered id=%u actor=%p\n",generation,next_id-1,a);}
}
void scan_step(){
    const auto now=GetTickCount64();if(scan_pause&&now<scan_pause)return;
    auto ws=reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))+kWorld);
    if(!readable(ws,8)||!*ws)return; void *w=*ws;
    if(known_world&&known_world!=w){known_world=w;reset_registry("world changed");}else if(!known_world)known_world=w;
    auto levels=reinterpret_cast<Array*>(reinterpret_cast<std::uintptr_t>(w)+0x178);
    if(!readable(levels,sizeof(Array))||levels->count<1||levels->count>1024||!readable(levels->data,levels->count*sizeof(void*)))return;
    int budget=4;
    while(budget&&scan_level<levels->count){void *lev=static_cast<void**>(levels->data)[scan_level];
        if(!readable(lev,0xB0)){++scan_level;scan_actor=0;continue;}
        auto actors=reinterpret_cast<Array*>(reinterpret_cast<std::uintptr_t>(lev)+0xA0);
        if(!readable(actors,sizeof(Array))||actors->count<0||actors->count>200000||
           (actors->count&&!readable(actors->data,actors->count*sizeof(void*)))){++scan_level;scan_actor=0;continue;}
        while(budget&&scan_actor<actors->count){remember(static_cast<void**>(actors->data)[scan_actor++]);--budget;}
        if(scan_actor>=actors->count){++scan_level;scan_actor=0;}}
    if(scan_level>=levels->count){scan_level=scan_actor=0;scan_pause=now+1500;}
}
void *automatic(int i){
    Vector o{};if(!pawn[i]||!location(pawn[i],o))return nullptr;
    auto r=*reinterpret_cast<Rotator*>(reinterpret_cast<std::uintptr_t>(pawn[i])+0x2088);
    const double rad=r.yaw*3.141592653589793/180.0,fx=std::cos(rad),fy=std::sin(rad);
    void *best=nullptr;double bestscore=1e30;
    for(int n=0;n<candidate_count;++n){void *c=candidates[n].actor;Vector p{};
        if(!c||c==pawn[i]||!alive(c)||!location(c,p))continue;
        double x=p.x-o.x,y=p.y-o.y,z=p.z-o.z,d=std::sqrt(x*x+y*y+z*z);if(d<10||d>7500)continue;
        double h=std::sqrt(x*x+y*y);double a=h>1?(x*fx+y*fy)/h:1;
        if(a<=0)continue; // automatic selection is limited to the visible forward hemisphere
        double score=d*(1.0+(1.0-a)*2.25);if(score<bestscore){bestscore=score;best=c;}}
    std::fprintf(g_log,"LOCK P%d acquire result=%p score=%.1f candidates=%d\n",i+1,best,bestscore,candidate_count);
    return best;
}
void *next_by_distance(int i){
    struct E{void*a;double d;unsigned id;} e[128]{};int count=0;Vector o{};if(!location(pawn[i],o))return nullptr;
    for(int n=0;n<candidate_count&&count<128;++n){void*c=candidates[n].actor;Vector p{};
        if(!c||c==pawn[i]||!alive(c)||!location(c,p))continue;double x=p.x-o.x,y=p.y-o.y,z=p.z-o.z;
        double d=x*x+y*y+z*z;if(d<100||d>7500.0*7500.0)continue;e[count++]={c,d,candidates[n].id};}
    for(int a=0;a<count;++a)for(int b=a+1;b<count;++b)if(e[b].d<e[a].d){E t=e[a];e[a]=e[b];e[b]=t;}
    std::fprintf(g_log,"LOCK P%d cycle eligible=%d previous=%p\n",i+1,count,target[i]);
    if(!count)return nullptr;for(int n=0;n<count;++n)if(e[n].a==target[i])return e[(n+1)%count].a;return e[0].a;
}
void set_arm(int i,bool active){
    arm[i]=active;if(active&&!previous_arm[i]&&enabled[i]){clear_applied(i);target[i]=automatic(i);if(target[i])apply(i);}
    if(!active&&previous_arm[i]){clear_applied(i);target[i]=nullptr;last_turn[i]=cycle_boost_until[i]=0;last_turn_clock[i]=0;}
    previous_arm[i]=active;
}
void toggle(int i){
    enabled[i]=!enabled[i];if(!enabled[i]){clear_applied(i);target[i]=nullptr;cycle_boost_until[i]=0;}
    else if(arm[i]&&pawn[i]){target[i]=automatic(i);if(target[i])apply(i);}
    std::fprintf(g_log,"LOCK P%d custom=%s\n",i+1,enabled[i]?"enabled":"disabled");std::fflush(g_log);
    if(g_record_file)std::fprintf(g_record_file,"LOCK-TOGGLE t=%llu p=%d enabled=%d\n",
        static_cast<unsigned long long>(GetTickCount64()),i+1,enabled[i]?1:0);
}
void cycle(int i){
    if(!enabled[i]||!arm[i]||!pawn[i])return;void *n=next_by_distance(i);if(n){target[i]=n;cycle_boost_until[i]=GetTickCount64()+1500;apply(i);}
    std::fprintf(g_log,"LOCK P%d cycle result=%p\n",i+1,n);std::fflush(g_log);
    if(g_record_file)std::fprintf(g_record_file,"LOCK-CYCLE t=%llu p=%d target=%p\n",
        static_cast<unsigned long long>(GetTickCount64()),i+1,n);
}
void update(){
    static LONGLONG last_update=0;
    const LONGLONG precise_now=clock_now();
    if(last_update&&precise_now-last_update<cadence_ticks(cadence_ms()))return;
    last_update=precise_now;
    const ULONGLONG now_tick=GetTickCount64();
    const bool available=!g_settings||InterlockedCompareExchange(&g_settings->lockon_available,0,0)!=0;
    if(!available){for(int i=0;i<2;++i)clear_applied(i);return;}
    // Fighter discovery is unrelated to smooth turning. Keep its previous
    // cadence while allowing already-selected targets to turn more often.
    static ULONGLONG last_discovery=0;
    if(!last_discovery||now_tick-last_discovery>=16){
        last_discovery=now_tick;
        scan_step();void *now[2]{};get_players(now);
        if((pawn[0]&&now[0]&&pawn[0]!=now[0])||(pawn[1]&&now[1]&&pawn[1]!=now[1]))reset_registry("player pawn changed");
        pawn[0]=now[0];pawn[1]=now[1];remember(now[0]);remember(now[1]);
    }
    if(g_settings){enabled[0]=InterlockedCompareExchange(&g_settings->lockon_player1,0,0)!=0;
        enabled[1]=InterlockedCompareExchange(&g_settings->lockon_player2,0,0)!=0;}
    // The launcher also uses mouse buttons. Never let its clicks acquire a game target.
    const bool game_focused=GetForegroundWindow()==g_window;
    const bool mouse_held=(GetAsyncKeyState(VK_LBUTTON)&0x8000)||(GetAsyncKeyState(VK_RBUTTON)&0x8000);
    static bool previous_focus=false, wait_for_release=true;
    if(game_focused!=previous_focus){
        std::fprintf(g_log,"LOCK P1 game_focus=%d wait_for_release=%d mouse_held=%d\n",
            game_focused?1:0,wait_for_release?1:0,mouse_held?1:0);
        if(g_record_file)std::fprintf(g_record_file,"LOCK-FOCUS t=%llu game=%d wait=%d mouse=%d\n",
            static_cast<unsigned long long>(GetTickCount64()),game_focused?1:0,wait_for_release?1:0,mouse_held?1:0);
        previous_focus=game_focused;
    }
    if(!game_focused)wait_for_release=true;
    else if(wait_for_release&&!mouse_held){
        wait_for_release=false;
        if(g_record_file)std::fprintf(g_record_file,"LOCK-FOCUS-REARM t=%llu\n",
            static_cast<unsigned long long>(GetTickCount64()));
    }
    set_arm(0,game_focused&&!wait_for_release&&mouse_held);
    static bool z=false,tab=false;bool nz=game_focused&&(GetAsyncKeyState('Z')&0x8000)!=0,
        nt=game_focused&&(GetAsyncKeyState(VK_TAB)&0x8000)!=0;
    if(nz&&!z){toggle(0);if(g_settings)InterlockedExchange(&g_settings->lockon_player1,enabled[0]?1:0);}if(nt&&!tab)cycle(0);z=nz;tab=nt;
    for(int i=0;i<2;++i){if(target[i]&&!alive(target[i])){
        auto b=reinterpret_cast<std::uintptr_t>(target[i]);
        std::fprintf(g_log,"LOCK P%d target disengaged actor=%p dead=%u fallen=%u downed=%u\n",
            i+1,target[i],
            readable(reinterpret_cast<void*>(b+kDead),1)?*reinterpret_cast<unsigned char*>(b+kDead):255,
            readable(reinterpret_cast<void*>(b+kFallen),1)?*reinterpret_cast<unsigned char*>(b+kFallen):255,
            readable(reinterpret_cast<void*>(b+kDowned),1)?*reinterpret_cast<unsigned char*>(b+kDowned):255);
        clear_applied(i);target[i]=nullptr;cycle_boost_until[i]=0;}
        if(arm[i]&&target[i])apply(i);}
    if(GetTickCount64()-last_diag>1000){last_diag=GetTickCount64();std::fprintf(g_log,
        "LOCK status generation=%u registry=%d enabled=(%d,%d) arms=(%d,%d) pawns=(%p,%p) targets=(%p,%p)\n",
        generation,candidate_count,enabled[0],enabled[1],arm[0],arm[1],pawn[0],pawn[1],target[0],target[1]);std::fflush(g_log);}
}
}
