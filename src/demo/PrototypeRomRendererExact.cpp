#include "demo/PrototypeRomRenderer.h"

#include <GL/gl.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <limits>

namespace promptemon::demo {
namespace {
constexpr float kPi = 3.14159265358979323846f;

std::string lower(std::string v) {
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return v;
}
bool endsWith(const std::string& v, const std::string& s) {
    return v.size() >= s.size() && v.compare(v.size() - s.size(), s.size(), s) == 0;
}
bool hasAny(const std::string& value, const std::vector<std::string>& tokens) {
    const auto v = lower(value);
    for (const auto& t : tokens) if (v.find(lower(t)) != std::string::npos) return true;
    return false;
}
float yawToward(float x, float z, float tx, float tz) {
    return std::atan2(tx - x, -(tz - z)) * 180.0f / kPi;
}

bch::Vec2 materialUv(const bch::Material* material, bch::Vec2 uv) {
    if (!material) return uv;
    const float c = std::cos(material->uvRotation), s = std::sin(material->uvRotation);
    const float x = uv.x - .5f, y = uv.y - .5f;
    return {(x*c-y*s+.5f)*material->uvScaleU-material->uvTranslateU,
            (x*s+y*c+.5f)*material->uvScaleV-material->uvTranslateV};
}

void drawMeshes(bch::Scene& scene,
                const std::unordered_map<std::string,unsigned int>& textures,
                const bch::Animation* animation,
                float frame) {
    const auto pose = scene.samplePose(animation, frame);
    for (const auto& mesh : scene.meshes()) {
        const auto* tex = scene.textureForMaterial(mesh.materialId);
        const bch::Material* material = mesh.materialId < scene.materials().size() ? &scene.materials()[mesh.materialId] : nullptr;
        GLuint id = 0;
        if (tex) {
            const auto it = textures.find(tex->name);
            if (it != textures.end()) id = static_cast<GLuint>(it->second);
        }
        if (id) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,id); glColor4f(1,1,1,1); }
        else { glDisable(GL_TEXTURE_2D); glColor4f(.82f,.84f,.82f,1); }
        glBegin(GL_TRIANGLES);
        for (const auto& vertex : mesh.vertices) {
            const auto p = scene.skin(vertex, pose);
            if (id) { const auto uv = materialUv(material,vertex.uv); glTexCoord2f(uv.x,1.0f-uv.y); }
            glVertex3f(p.x,p.y,p.z);
        }
        glEnd();
    }
    glBindTexture(GL_TEXTURE_2D,0);
    glDisable(GL_TEXTURE_2D);
}

std::string bySuffix(storage::RomAssetCache* assets, const std::vector<std::string>& suffixes) {
    if (!assets) return {};
    for (const auto& wanted : suffixes) {
        const std::string s = lower(wanted);
        for (const auto& path : assets->modelResources()) if (endsWith(lower(path),s)) return path;
    }
    return {};
}

std::string canonical(storage::RomAssetCache* assets, const std::string& requested, const std::wstring& name) {
    if (name == L"Rattata") { auto p=bySuffix(assets,{"/koratta.bch","/koratta_f.bch"}); if(!p.empty()) return p; }
    if (name == L"Pidgey")  { auto p=bySuffix(assets,{"/poppo.bch"}); if(!p.empty()) return p; }
    if (name == L"Caterpie"){ auto p=bySuffix(assets,{"/kyatapi.bch","/kyatapii.bch"}); if(!p.empty()) return p; }
    if (name == L"Weedle")  { auto p=bySuffix(assets,{"/biidoru.bch"}); if(!p.empty()) return p; }
    if (name == L"Zubat")   { auto p=bySuffix(assets,{"/zubatto.bch","/zubatto_f.bch"}); if(!p.empty()) return p; }
    if (name == L"Geodude") { auto p=bySuffix(assets,{"/isitubute.bch","/ishitsubute.bch"}); if(!p.empty()) return p; }
    if (name == L"Gastly")  { auto p=bySuffix(assets,{"/goosu.bch"}); if(!p.empty()) return p; }
    if (name == L"Machop")  { auto p=bySuffix(assets,{"/wanrikii.bch"}); if(!p.empty()) return p; }
    return requested;
}
}

PrototypeRomRenderer::~PrototypeRomRenderer(){ destroy(); }

bool PrototypeRomRenderer::create(HWND parent, storage::RomAssetCache* assets) {
    destroy(); parent_=parent; archive_=assets;
    if(!archive_ || !archive_->isOpen()) return false;
    static bool registered=false;
    if(!registered){ WNDCLASSW wc{}; wc.style=CS_OWNDC|CS_HREDRAW|CS_VREDRAW; wc.lpfnWndProc=WindowProc; wc.hInstance=GetModuleHandleW(nullptr); wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.lpszClassName=L"PromptemonGameSurface"; if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return false; registered=true; }
    window_=CreateWindowExW(0,L"PromptemonGameSurface",L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|WS_CLIPCHILDREN,0,0,1,1,parent,nullptr,GetModuleHandleW(nullptr),this);
    if(!window_ || !initializeOpenGL()) return false;
    initializeFonts();
    if(!uiSkin_.ready()) { destroy(); return false; }
    lastTick_=std::chrono::steady_clock::now();
    return true;
}

void PrototypeRomRenderer::destroy(){
    if(gl_){ wglMakeCurrent(dc_,gl_); releaseStageComposition(); releaseVisual(avatar_); releaseVisual(player_); releaseVisual(wild_); releaseVisual(third_); releaseVisual(effect_); releaseFonts(); }
    shutdownOpenGL(); if(window_&&IsWindow(window_)) DestroyWindow(window_); window_=nullptr; parent_=nullptr; archive_=nullptr;
}
void PrototypeRomRenderer::show(bool v){ if(window_) ShowWindow(window_,v?SW_SHOW:SW_HIDE); }
void PrototypeRomRenderer::move(int x,int y,int w,int h){ if(window_) MoveWindow(window_,x,y,std::max(1,w),std::max(1,h),TRUE); }

bool PrototypeRomRenderer::initializeOpenGL(){
    dc_=GetDC(window_); if(!dc_) return false;
    PIXELFORMATDESCRIPTOR p{}; p.nSize=sizeof(p); p.nVersion=1; p.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER; p.iPixelType=PFD_TYPE_RGBA; p.cColorBits=32; p.cDepthBits=24; p.iLayerType=PFD_MAIN_PLANE;
    const int f=ChoosePixelFormat(dc_,&p); if(!f||!SetPixelFormat(dc_,f,&p)) return false; gl_=wglCreateContext(dc_); return gl_&&wglMakeCurrent(dc_,gl_)==TRUE;
}
void PrototypeRomRenderer::shutdownOpenGL(){ if(gl_){wglMakeCurrent(nullptr,nullptr);wglDeleteContext(gl_);gl_=nullptr;} if(dc_&&window_){ReleaseDC(window_,dc_);dc_=nullptr;} }
void PrototypeRomRenderer::initializeFonts(){ if(archive_&&gl_){wglMakeCurrent(dc_,gl_);uiSkin_.load(*archive_);} }
void PrototypeRomRenderer::releaseFonts(){uiSkin_.release();}

std::vector<std::uint8_t> PrototypeRomRenderer::animationForModel(const std::string& modelPath) const {
    if(!archive_) return {};
    const auto p=lower(modelPath);
    if(p.find("mii/")!=std::string::npos && archive_->has("mii/mii_anim.bch")) { try{return archive_->extractBch("mii/mii_anim.bch");}catch(...){} }
    const std::string stem=lower(std::filesystem::path(modelPath).stem().string());
    for(const auto& r:archive_->modelResources()){
        const auto q=lower(r); if(q==p||q.find(stem)==std::string::npos) continue;
        if(q.find("anim")==std::string::npos&&q.find("motion")==std::string::npos) continue;
        try{return archive_->extractBch(r);}catch(...){}
    }
    return {};
}

bool PrototypeRomRenderer::ensureVisual(Visual& v,const std::string& path,bool anim){ if(path.empty())return false; if(v.scene&&v.modelPath==path)return true; return loadVisual(v,path,anim); }
bool PrototypeRomRenderer::loadVisual(Visual& v,const std::string& path,bool anim){
    if(!archive_||path.empty())return false; wglMakeCurrent(dc_,gl_); releaseVisual(v);
    try{
        const auto model=archive_->extractBch(path); const auto motion=anim?animationForModel(path):std::vector<std::uint8_t>{};
        auto scene=std::make_unique<bch::Scene>(motion.empty()?bch::Scene::parse(model):bch::Scene::parse(model,motion)); if(scene->meshes().empty())return false;
        v.modelPath=path; v.scene=std::move(scene); v.frame=0;
        if(anim){ v.idle=v.scene->findAnimation("Pii_anim_nomove_kamae"); if(!v.idle)v.idle=chooseAnimation(v,{"idle","wait","stand","nomove","kamae","fly"},false); if(!v.idle)for(const auto& c:v.scene->animations())if(c.loop){v.idle=&c;break;} v.current=v.idle; }
        updateBounds(v); uploadTextures(v); return true;
    }catch(...){releaseVisual(v);return false;}
}
void PrototypeRomRenderer::releaseVisual(Visual& v){ for(auto& t:v.textures){GLuint id=t.second;if(id)glDeleteTextures(1,&id);} v={}; }
void PrototypeRomRenderer::uploadTextures(Visual& v){
    if(!v.scene)return; glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    for(const auto& t:v.scene->textures()){ if(t.width<=0||t.height<=0||t.rgba.empty())continue; GLuint id=0;glGenTextures(1,&id);if(!id)continue;glBindTexture(GL_TEXTURE_2D,id);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,t.width,t.height,0,GL_RGBA,GL_UNSIGNED_BYTE,t.rgba.data());v.textures[t.name]=id; }
    glBindTexture(GL_TEXTURE_2D,0);
}
void PrototypeRomRenderer::updateBounds(Visual& v){
    if(!v.scene)return; bch::Vec3 mn{FLT_MAX,FLT_MAX,FLT_MAX},mx{-FLT_MAX,-FLT_MAX,-FLT_MAX}; bool ok=false; const auto pose=v.scene->samplePose(nullptr,0);
    for(const auto& m:v.scene->meshes())for(const auto& vert:m.vertices){const auto p=v.scene->skin(vert,pose);mn.x=std::min(mn.x,p.x);mn.y=std::min(mn.y,p.y);mn.z=std::min(mn.z,p.z);mx.x=std::max(mx.x,p.x);mx.y=std::max(mx.y,p.y);mx.z=std::max(mx.z,p.z);ok=true;}
    if(!ok)return;v.boundsMin=mn;v.boundsMax=mx;v.center={(mn.x+mx.x)*.5f,(mn.y+mx.y)*.5f,(mn.z+mx.z)*.5f};const float e=std::max({mx.x-mn.x,mx.y-mn.y,mx.z-mn.z});if(e>1e-5f)v.fitScale=1.75f/e;
}

void PrototypeRomRenderer::releaseStageComposition(){for(auto& p:stageParts_)releaseVisual(p);stageParts_.clear();stageDirectory_.clear();stageBoundsMin_={};stageBoundsMax_={};}
bool PrototypeRomRenderer::loadStageComposition(const std::string& requested){
    releaseStageComposition(); if(!archive_)return false;
    std::string ground=bySuffix(archive_,{"stage/grassland/ground_01.bch"}); if(ground.empty())ground=requested; if(ground.empty())return false;
    Visual visible; if(!loadVisual(visible,ground,false))return false; stageParts_.push_back(std::move(visible)); stagePath_=ground;
    const auto slash=lower(ground).find_last_of('/'); stageDirectory_=slash==std::string::npos?std::string{}:lower(ground).substr(0,slash+1);
    std::string attr=bySuffix(archive_,{"stage/grassland/attr_01.bch"});
    Visual bounds; if(!attr.empty()&&loadVisual(bounds,attr,false)){stageBoundsMin_=bounds.boundsMin;stageBoundsMax_=bounds.boundsMax;releaseVisual(bounds);} else {stageBoundsMin_=stageParts_[0].boundsMin;stageBoundsMax_=stageParts_[0].boundsMax;}
    return true;
}
void PrototypeRomRenderer::updateStageCompositionBounds(){ if(!stageParts_.empty()){stageBoundsMin_=stageParts_[0].boundsMin;stageBoundsMax_=stageParts_[0].boundsMax;} }

const bch::Animation* PrototypeRomRenderer::chooseAnimation(Visual& v,const std::vector<std::string>& tokens,bool nonLoop) const {
    if(!v.scene)return nullptr; const bch::Animation* best=nullptr;int scoreBest=-99999;
    for(const auto& c:v.scene->animations()){int score=0;const auto n=lower(c.name);bool matched=false;for(const auto& t:tokens)if(n.find(lower(t))!=std::string::npos){matched=true;score+=100;}if(!matched)continue;if(nonLoop&&!c.loop)score+=40;if(nonLoop&&c.loop)score-=20;if(c.frames>5)score+=10;if(score>scoreBest){scoreBest=score;best=&c;}}
    return best;
}
void PrototypeRomRenderer::playAnimation(Visual& v,const std::vector<std::string>& tokens,bool nonLoop){if(const auto* a=chooseAnimation(v,tokens,nonLoop)){v.current=a;v.frame=0;}}

void PrototypeRomRenderer::setStageModel(const std::string& p){stagePath_=p;loadStageComposition(p);}
void PrototypeRomRenderer::setEffectModel(const std::string& p){effectPath_=p;if(!p.empty())ensureVisual(effect_,p,true);}

void PrototypeRomRenderer::showMainMenu(){
    page_=GamePage::MainMenu;if(stageParts_.empty())loadStageComposition(stagePath_);
    const auto hooh=bySuffix(archive_,{"/houou.bch"});const auto lugia=bySuffix(archive_,{"/rugia.bch"});
    ensureVisual(player_,hooh,true);ensureVisual(wild_,lugia,true);menuAttackBeat_=-1;render();
}
void PrototypeRomRenderer::showIntro(){page_=GamePage::Intro;render();}
void PrototypeRomRenderer::showOptions(int v,bool f){page_=GamePage::Options;volume_=v;fullscreen_=f;render();}
bool PrototypeRomRenderer::showStarterSelect(const std::string&a,const std::string&b,const std::string&c){
    page_=GamePage::StarterSelect;const auto mii=bySuffix(archive_,{"/mii_normal.bch","/mii_bare.bch"});ensureVisual(avatar_,mii,true);
    const bool x=ensureVisual(player_,canonical(archive_,a,L"Rattata"),true),y=ensureVisual(wild_,canonical(archive_,b,L"Pidgey"),true),z=ensureVisual(third_,canonical(archive_,c,L"Caterpie"),true);render();return x&&y&&z;
}
bool PrototypeRomRenderer::showHub(const std::string&m,const std::wstring&n,int l,int xp,int w,int lo,const std::wstring&iv){page_=GamePage::Hub;playerName_=n;playerLevel_=l;xp_=xp;wins_=w;losses_=lo;ivSummary_=iv;const bool ok=ensureVisual(player_,canonical(archive_,m,n),true);render();return ok;}
bool PrototypeRomRenderer::showHunt(const std::string&m,const std::wstring&n,int l,const std::wstring&iv,bool s){page_=GamePage::Hunt;wildName_=n;wildLevel_=l;ivReward_=iv;shiny_=s;const bool ok=ensureVisual(wild_,canonical(archive_,m,n),true);render();return ok;}
bool PrototypeRomRenderer::showBattle(const std::string&pm,const std::string&wm,const std::wstring&pn,int pl,int php,int pmax,const std::wstring&wn,int wl,int whp,int wmax,const std::wstring&iv,bool s,const std::wstring&m1,const std::wstring&m2,const std::wstring&log,bool over,bool won){page_=GamePage::Battle;playerName_=pn;playerLevel_=pl;playerHp_=php;playerMaxHp_=std::max(1,pmax);wildName_=wn;wildLevel_=wl;wildHp_=whp;wildMaxHp_=std::max(1,wmax);ivReward_=iv;shiny_=s;move1_=m1;move2_=m2;battleLog_=log;battleOver_=over;battleWon_=won;const bool a=ensureVisual(player_,canonical(archive_,pm,pn),true),b=ensureVisual(wild_,canonical(archive_,wm,wn),true);playerFieldX_=-3.4f;playerFieldZ_=2.0f;wildFieldX_=3.1f;wildFieldZ_=-1.8f;render();return a&&b;}

void PrototypeRomRenderer::playPlayerAction(){playAnimation(player_,{"waza","attack","rush","move"},true);effectTargetsWild_=true;effectTimer_=1;}
void PrototypeRomRenderer::playWildAction(){playAnimation(wild_,{"waza","attack","rush","move"},true);effectTargetsWild_=false;effectTimer_=1;}
void PrototypeRomRenderer::playPlayerHit(){playAnimation(player_,{"damage","hit","yarare"},true);} void PrototypeRomRenderer::playWildHit(){playAnimation(wild_,{"damage","hit","yarare"},true);} void PrototypeRomRenderer::playPlayerFaint(){playAnimation(player_,{"down","dead","faint"},true);} void PrototypeRomRenderer::playWildFaint(){playAnimation(wild_,{"down","dead","faint"},true);}

void PrototypeRomRenderer::setupPerspective(int w,int h){glViewport(0,0,std::max(1,w),std::max(1,h));glMatrixMode(GL_PROJECTION);glLoadIdentity();const double a=static_cast<double>(std::max(1,w))/std::max(1,h);const double top=.34;glFrustum(-top*a,top*a,-top,top,.55,300);glMatrixMode(GL_MODELVIEW);glLoadIdentity();}
void PrototypeRomRenderer::setupOverlay(int w,int h){glViewport(0,0,std::max(1,w),std::max(1,h));glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,w,h,0,-1,1);glMatrixMode(GL_MODELVIEW);glLoadIdentity();}
bool PrototypeRomRenderer::playerInAttackRange()const{return page_==GamePage::Battle&&!battleOver_;}
void PrototypeRomRenderer::updateBattleInput(float){if(page_!=GamePage::Battle||battleOver_)return;const bool a=(GetAsyncKeyState('Z')&0x8000)||(GetAsyncKeyState(VK_SPACE)&0x8000),b=(GetAsyncKeyState('X')&0x8000);if(a&&!attack1Latch_&&parent_&&effectTimer_<=0)SendMessageW(parent_,WM_COMMAND,MAKEWPARAM(static_cast<int>(GameAction::Move1),BN_CLICKED),0);if(b&&!attack2Latch_&&parent_&&effectTimer_<=0)SendMessageW(parent_,WM_COMMAND,MAKEWPARAM(static_cast<int>(GameAction::Move2),BN_CLICKED),0);attack1Latch_=a;attack2Latch_=b;}

void PrototypeRomRenderer::drawVisual(Visual& v,float x,float y,float z,float scale,float yaw){if(!v.scene)return;glPushMatrix();glTranslatef(x,y,z);glRotatef(yaw,0,1,0);const float f=v.fitScale*scale;glScalef(f,f,f);glTranslatef(-v.center.x,-v.center.y,-v.center.z);drawMeshes(*v.scene,v.textures,v.current,v.frame);glPopMatrix();}
void PrototypeRomRenderer::drawStageComposition(){
    if(stageParts_.empty())return;const float sx=std::max(.001f,stageBoundsMax_.x-stageBoundsMin_.x),sz=std::max(.001f,stageBoundsMax_.z-stageBoundsMin_.z);const float cx=(stageBoundsMin_.x+stageBoundsMax_.x)*.5f,cz=(stageBoundsMin_.z+stageBoundsMax_.z)*.5f,fy=stageBoundsMin_.y;
    const float scale=std::max(30.0f/sx,22.0f/sz);glPushMatrix();glScalef(scale,scale,scale);glTranslatef(-cx,-fy,-cz);for(auto& p:stageParts_)if(p.scene)drawMeshes(*p.scene,p.textures,nullptr,0);glPopMatrix();
}
void PrototypeRomRenderer::drawShadow(float x,float y,float z,float rx,float rz,float a){glDisable(GL_TEXTURE_2D);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glColor4f(.02f,.02f,.02f,a);glBegin(GL_TRIANGLE_FAN);glVertex3f(x,y,z);for(int i=0;i<=24;++i){const float t=i/24.0f*2*kPi;glVertex3f(x+std::cos(t)*rx,y,z+std::sin(t)*rz);}glEnd();}
void PrototypeRomRenderer::drawFallbackEffect(float x,float y,float z,float p){glDisable(GL_TEXTURE_2D);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);const float r=.15f+std::sin(std::clamp(p,0.0f,1.0f)*kPi)*.75f;glColor4f(1,.86f,.2f,1-p);glBegin(GL_LINES);for(int i=0;i<12;++i){const float a=i/12.0f*2*kPi;glVertex3f(x,y,z);glVertex3f(x+std::cos(a)*r,y+.25f,z+std::sin(a)*r);}glEnd();}
void PrototypeRomRenderer::drawMenuBattleEffect(float x,float y,float z,float p){drawFallbackEffect(x,y,z,p);}
void PrototypeRomRenderer::drawAttackEffect(){if(effectTimer_<=0)return;const float p=1-effectTimer_,x=effectTargetsWild_?wildFieldX_:playerFieldX_,z=effectTargetsWild_?wildFieldZ_:playerFieldZ_;if(effect_.scene)drawVisual(effect_,x,.5f,z,.45f,0);else drawFallbackEffect(x,.5f,z,p);}

void PrototypeRomRenderer::renderWorld(int w,int h){
    setupPerspective(w,h);glEnable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);const float t=elapsedSeconds_;glPushMatrix();
    const float sway=std::sin(t*.32f)*3.0f,slide=std::sin(t*.24f)*.55f,lift=std::sin(t*.41f)*.14f;
    glTranslatef(slide,-3.25f+lift,-15.7f);glRotatef(39.0f+std::sin(t*.18f)*1.1f,1,0,0);glRotatef(sway,0,1,0);drawStageComposition();
    if(page_==GamePage::MainMenu||page_==GamePage::Intro||page_==GamePage::Options){
        const float cycle=std::fmod(t,6.0f);float ax=-4.4f,az=1.2f,bx=4.2f,bz=-1.1f;float p=0;
        if(cycle<1.2f){p=std::sin(cycle/1.2f*kPi);ax+=p*2.8f;az-=p*1.2f;drawMenuBattleEffect(bx,.65f,bz,cycle/1.2f);} else if(cycle>=3&&cycle<4.2f){p=std::sin((cycle-3)/1.2f*kPi);bx-=p*2.8f;bz+=p*1.2f;drawMenuBattleEffect(ax,.65f,az,(cycle-3)/1.2f);}
        drawShadow(ax,.02f,az,.65f,.38f,.28f);drawShadow(bx,.02f,bz,.65f,.38f,.28f);drawVisual(player_,ax,.62f+std::sin(t*2.1f)*.10f,az,1.0f,yawToward(ax,az,bx,bz));drawVisual(wild_,bx,.62f+std::sin(t*2.0f+1)*.10f,bz,1.0f,yawToward(bx,bz,ax,az));glPopMatrix();return;
    }
    if(page_==GamePage::StarterSelect){
        drawShadow(0,.02f,3.5f,.38f,.22f,.28f);drawVisual(avatar_,0,.48f,3.5f,.75f,180);drawVisual(player_,-3.0f,.42f,-.2f,.55f,15);drawVisual(wild_,0,.42f,-1.3f,.55f,0);drawVisual(third_,3.0f,.42f,-.2f,.55f,-15);glPopMatrix();return;
    }
    if(page_==GamePage::Hub){drawVisual(player_,0,.42f,1.2f,.55f,180);}
    else if(page_==GamePage::Hunt){drawVisual(player_,-2.6f,.42f,1.8f,.55f,35);drawVisual(wild_,2.5f,.42f,-1.4f,.55f,-35);}
    else if(page_==GamePage::Battle){float px=playerFieldX_,pz=playerFieldZ_,wx=wildFieldX_,wz=wildFieldZ_;if(effectTimer_>0){const float q=std::sin((1-effectTimer_)*kPi),dx=wx-px,dz=wz-pz,d=std::max(.001f,std::sqrt(dx*dx+dz*dz));if(effectTargetsWild_){px+=dx/d*q*1.5f;pz+=dz/d*q*1.5f;}else{wx-=dx/d*q*1.5f;wz-=dz/d*q*1.5f;}}drawShadow(px,.02f,pz,.42f,.25f,.32f);drawShadow(wx,.02f,wz,.42f,.25f,.32f);drawVisual(player_,px,.44f,pz,.60f,yawToward(px,pz,wx,wz));drawVisual(wild_,wx,.44f,wz,.60f,yawToward(wx,wz,px,pz));drawAttackEffect();}
    glPopMatrix();
}

void PrototypeRomRenderer::drawPanel(float x,float y,float w,float h,float a){uiSkin_.drawPanel(x,y,w,h,a);} float PrototypeRomRenderer::textWidth(const std::wstring&t,int k)const{return uiSkin_.textWidth(t,k);} void PrototypeRomRenderer::drawText(float x,float y,const std::wstring&t,int k,bool c){uiSkin_.drawText(x,y,t,k,c);} void PrototypeRomRenderer::addHitTarget(GameAction a,const RectF&r,bool e){if(e&&a!=GameAction::None)hitTargets_.push_back({a,r});}
void PrototypeRomRenderer::drawButton(const RectF&r,const std::wstring&l,GameAction a,bool e){uiSkin_.drawButton(r.x,r.y,r.w,r.h,e&&a==hoverAction_,e);drawText(r.x+r.w*.5f,r.y+r.h*.62f,l,1,true);addHitTarget(a,r,e);}
void PrototypeRomRenderer::drawHpPanel(const RectF&r,const std::wstring&n,int level,int hp,int maxHp,bool shiny,bool){drawPanel(r.x,r.y,r.w,r.h,.9f);drawText(r.x+14,r.y+26,(shiny?L"★ ":L"")+n,1,false);drawText(r.x+r.w-72,r.y+26,L"Nv."+std::to_wstring(level),0,false);const float ratio=std::clamp(static_cast<float>(hp)/std::max(1,maxHp),0.0f,1.0f),x=r.x+14,y=r.y+43,w=r.w-28;glDisable(GL_TEXTURE_2D);glColor4f(.05f,.08f,.08f,.9f);glBegin(GL_QUADS);glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+9);glVertex2f(x,y+9);glEnd();glColor4f(ratio>.5f?.25f:.95f,ratio>.5f?.86f:(ratio>.2f?.72f:.2f),ratio>.5f?.36f:.18f,1);glBegin(GL_QUADS);glVertex2f(x,y);glVertex2f(x+w*ratio,y);glVertex2f(x+w*ratio,y+9);glVertex2f(x,y+9);glEnd();}
std::wstring PrototypeRomRenderer::cropText(const std::wstring&v,std::size_t m)const{return v.size()<=m?v:(m<4?v.substr(0,m):v.substr(0,m-3)+L"...");}

void PrototypeRomRenderer::renderOverlay(int width,int height){
    setupOverlay(width,height);glDisable(GL_DEPTH_TEST);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);hitTargets_.clear();const float W=static_cast<float>(width),H=static_cast<float>(height);
    switch(page_){
        case GamePage::MainMenu:{drawText(54,74,L"PROMPTEMON",2,false);drawText(56,104,L"STEIDIUM",1,false);const float bw=300;float y=H*.43f;drawButton({54,y,bw,44},L"Nueva partida",GameAction::NewGame,true);y+=50;drawButton({54,y,bw,44},L"Cargar partida",GameAction::None,false);y+=50;drawButton({54,y,bw,44},L"Borrar partida",GameAction::None,false);y+=50;drawButton({54,y,bw,44},L"Exportar partida",GameAction::None,false);y+=50;drawButton({54,y,bw,44},L"Opciones",GameAction::Options,true);break;}
        case GamePage::Intro:drawPanel(46,H-188,W-92,130,.88f);drawText(70,H-150,L"Bienvenido a Promptemon Steidium",1,false);drawButton({W-246,H-104,176,40},L"Continuar",GameAction::IntroContinue,true);break;
        case GamePage::StarterSelect:{drawText(W*.5f,58,L"ELIGE TU PRIMER COMPAÑERO",2,true);const float cw=W/3;drawButton({16,H-80,cw-28,48},L"Rattata | IV Velocidad",GameAction::StarterRattata,true);drawButton({cw+6,H-80,cw-28,48},L"Pidgey | IV Velocidad",GameAction::StarterPidgey,true);drawButton({cw*2+6,H-80,cw-22,48},L"Caterpie | IV PS",GameAction::StarterCaterpie,true);break;}
        case GamePage::Hub:drawPanel(24,22,420,86,.82f);drawText(42,52,playerName_+L"  Nv."+std::to_wstring(playerLevel_),1,false);drawText(42,80,L"XP "+std::to_wstring(xp_)+L"/100",0,false);drawButton({W-244,H-66,210,40},L"Entrar a Caza",GameAction::Hunt,true);break;
        case GamePage::Hunt:drawPanel(24,22,330,80,.82f);drawText(42,52,wildName_+L"  Nv."+std::to_wstring(wildLevel_),1,false);drawText(42,80,L"IV: "+ivReward_,0,false);drawButton({W-220,H-64,188,38},L"Combatir",GameAction::StartBattle,true);break;
        case GamePage::Battle:{const float pw=300;drawHpPanel({22,H-88,pw,66},playerName_,playerLevel_,playerHp_,playerMaxHp_,false,false);drawHpPanel({W-pw-22,20,pw,66},wildName_,wildLevel_,wildHp_,wildMaxHp_,shiny_,true);if(!battleOver_){drawButton({W-410,H-78,184,46},move1_,GameAction::Move1,effectTimer_<=0);drawButton({W-214,H-78,184,46},move2_,GameAction::Move2,effectTimer_<=0);}break;}
        case GamePage::Options:drawPanel(W*.22f,H*.18f,W*.56f,H*.60f,.9f);drawText(W*.5f,H*.27f,L"OPCIONES",2,true);drawText(W*.35f,H*.40f,L"Volumen "+std::to_wstring(volume_)+L"%",1,false);drawButton({W*.32f,H*.47f,90,44},L"-",GameAction::VolumeDown,volume_>0);drawButton({W*.60f,H*.47f,90,44},L"+",GameAction::VolumeUp,volume_<100);drawButton({W*.5f-90,H*.68f,180,42},L"Volver",GameAction::Back,true);break;
    }
    glEnable(GL_DEPTH_TEST);
}

void PrototypeRomRenderer::render(){if(!window_||!gl_)return;wglMakeCurrent(dc_,gl_);RECT r{};GetClientRect(window_,&r);const int w=std::max(1,(int)(r.right-r.left)),h=std::max(1,(int)(r.bottom-r.top));glViewport(0,0,w,h);glClearColor(.55f,.78f,.93f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);renderWorld(w,h);renderOverlay(w,h);SwapBuffers(dc_);}
void PrototypeRomRenderer::tick(){if(!gl_)return;const auto now=std::chrono::steady_clock::now();float dt=std::chrono::duration<float>(now-lastTick_).count();lastTick_=now;dt=std::clamp(dt,0.0f,.1f);elapsedSeconds_+=dt;effectTimer_=std::max(0.0f,effectTimer_-dt);updateBattleInput(dt);if(page_==GamePage::MainMenu){const int beat=(int)std::floor(std::fmod(elapsedSeconds_,6.0f));if(beat!=menuAttackBeat_){if(beat==0)playAnimation(player_,{"attack","waza","rush","move"},true);if(beat==3)playAnimation(wild_,{"attack","waza","rush","move"},true);menuAttackBeat_=beat;}}auto advance=[dt](Visual&v){if(!v.current)return;v.frame+=dt*30;if(v.current->frames>0&&v.frame>=v.current->frames){if(v.current->loop)v.frame=std::fmod(v.frame,v.current->frames);else{v.current=v.idle;v.frame=0;}}};advance(avatar_);advance(player_);advance(wild_);advance(third_);advance(effect_);render();}
GameAction PrototypeRomRenderer::hitTest(int x,int y)const{for(auto it=hitTargets_.rbegin();it!=hitTargets_.rend();++it){const auto&r=it->second;if(x>=r.x&&x<=r.x+r.w&&y>=r.y&&y<=r.y+r.h)return it->first;}return GameAction::None;}
LRESULT CALLBACK PrototypeRomRenderer::WindowProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){auto*self=reinterpret_cast<PrototypeRomRenderer*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));if(msg==WM_NCCREATE){auto*c=reinterpret_cast<CREATESTRUCTW*>(lp);self=static_cast<PrototypeRomRenderer*>(c->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}switch(msg){case WM_PAINT:{PAINTSTRUCT p{};BeginPaint(hwnd,&p);if(self)self->render();EndPaint(hwnd,&p);return 0;}case WM_ERASEBKGND:return 1;case WM_SIZE:if(self)self->render();return 0;case WM_MOUSEMOVE:if(self){const auto a=self->hitTest(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));if(a!=self->hoverAction_){self->hoverAction_=a;self->render();}}return 0;case WM_LBUTTONUP:if(self&&self->parent_){const auto a=self->hitTest(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));if(a!=GameAction::None)SendMessageW(self->parent_,WM_COMMAND,MAKEWPARAM(static_cast<int>(a),BN_CLICKED),0);}return 0;}return DefWindowProcW(hwnd,msg,wp,lp);}

} // namespace promptemon::demo
