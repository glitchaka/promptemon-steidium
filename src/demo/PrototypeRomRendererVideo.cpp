#include "demo/PrototypeRomRenderer.h"

#include <GL/gl.h>

#include <algorithm>
#include <cfloat>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <limits>
#include <set>

namespace promptemon::demo {
namespace {

constexpr float kPi = 3.14159265358979323846f;

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool collisionLike(const std::string& path) {
    const std::string p = lowerAscii(path);
    const auto slash = p.find_last_of('/');
    const std::string leaf = p.substr(slash == std::string::npos ? 0 : slash + 1);
    return leaf.rfind("attr", 0) == 0 || leaf.rfind("col_", 0) == 0 ||
           leaf.find("collision") != std::string::npos || leaf.find("collider") != std::string::npos;
}

std::string parentVirtualPath(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string{} : path.substr(0, slash + 1);
}

bch::Vec2 materialUv(const bch::Material* material, bch::Vec2 uv) {
    if (!material) return uv;
    const float c = std::cos(material->uvRotation);
    const float s = std::sin(material->uvRotation);
    const float x = uv.x - .5f;
    const float y = uv.y - .5f;
    return {(x * c - y * s + .5f) * material->uvScaleU - material->uvTranslateU,
            (x * s + y * c + .5f) * material->uvScaleV - material->uvTranslateV};
}

void drawMeshes(bch::Scene& scene,
                const std::unordered_map<std::string, unsigned int>& textures,
                const bch::Animation* animation,
                float frame) {
    const auto pose = scene.samplePose(animation, frame);
    for (const auto& mesh : scene.meshes()) {
        const auto* tex = scene.textureForMaterial(mesh.materialId);
        const bch::Material* material = mesh.materialId < scene.materials().size()
            ? &scene.materials()[mesh.materialId] : nullptr;

        GLuint id = 0;
        if (tex) {
            const auto found = textures.find(tex->name);
            if (found != textures.end()) id = static_cast<GLuint>(found->second);
        }
        if (id) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, id);
            glColor4f(1,1,1,1);
        } else {
            glDisable(GL_TEXTURE_2D);
            glColor4f(.84f,.86f,.82f,1);
        }

        glBegin(GL_TRIANGLES);
        for (const auto& vertex : mesh.vertices) {
            const auto p = scene.skin(vertex, pose);
            if (id) {
                const auto uv = materialUv(material, vertex.uv);
                glTexCoord2f(uv.x, 1.0f - uv.y);
            }
            glVertex3f(p.x,p.y,p.z);
        }
        glEnd();
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

std::string bySuffix(storage::RomAssetCache* assets, const std::vector<std::string>& suffixes) {
    if (!assets) return {};
    const auto models = assets->modelResources();
    for (const auto& wanted : suffixes) {
        const std::string suffix = lowerAscii(wanted);
        for (const auto& path : models) {
            if (endsWith(lowerAscii(path), suffix)) return path;
        }
    }
    return {};
}

std::string byTokens(storage::RomAssetCache* assets,
                     const std::vector<std::string>& required,
                     const std::vector<std::string>& preferred = {}) {
    if (!assets) return {};
    int bestScore = std::numeric_limits<int>::min();
    std::string best;
    for (const auto& path : assets->modelResources()) {
        const std::string p = lowerAscii(path);
        bool ok = true;
        for (const auto& token : required) {
            if (p.find(lowerAscii(token)) == std::string::npos) { ok = false; break; }
        }
        if (!ok || collisionLike(path)) continue;
        int score = 0;
        for (const auto& token : preferred)
            if (p.find(lowerAscii(token)) != std::string::npos) score += 40;
        if (p.find("ground") != std::string::npos) score += 60;
        if (p.find("floor") != std::string::npos) score += 50;
        if (p.find("stage") != std::string::npos) score += 20;
        if (score > bestScore) { bestScore = score; best = path; }
    }
    return best;
}

std::string townStage(storage::RomAssetCache* assets) {
    std::string p = byTokens(assets,{"stage","town1"},{"ground","floor"});
    if (p.empty()) p = byTokens(assets,{"stage","town"},{"ground","floor"});
    if (p.empty()) p = byTokens(assets,{"stage","castle"},{"ground","floor"});
    if (p.empty()) p = byTokens(assets,{"stage","courtyard"},{"ground","floor"});
    if (p.empty()) p = byTokens(assets,{"stage","plaza"},{"ground","floor"});
    return p;
}

std::string fieldStage(storage::RomAssetCache* assets) {
    std::string p = byTokens(assets,{"stage","grassland"},{"ground","floor"});
    if (p.empty()) p = byTokens(assets,{"stage","grass"},{"ground","floor"});
    if (p.empty()) p = byTokens(assets,{"stage","forest"},{"ground","floor"});
    if (p.empty()) p = byTokens(assets,{"stage","field"},{"ground","floor"});
    if (p.empty()) p = byTokens(assets,{"stage"},{"ground","floor"});
    return p;
}

std::string canonical(storage::RomAssetCache* assets, const std::string& requested, const std::wstring& name) {
    if (name == L"Rattata") { auto p=bySuffix(assets,{"/koratta.bch","/koratta_f.bch"}); if(!p.empty()) return p; }
    if (name == L"Pidgey") { auto p=bySuffix(assets,{"/poppo.bch"}); if(!p.empty()) return p; }
    if (name == L"Caterpie") { auto p=bySuffix(assets,{"/kyatapi.bch","/kyatapii.bch"}); if(!p.empty()) return p; }
    if (name == L"Weedle") { auto p=bySuffix(assets,{"/biidoru.bch"}); if(!p.empty()) return p; }
    if (name == L"Zubat") { auto p=bySuffix(assets,{"/zubatto.bch","/zubatto_f.bch"}); if(!p.empty()) return p; }
    if (name == L"Geodude") { auto p=bySuffix(assets,{"/isitubute.bch","/ishitsubute.bch"}); if(!p.empty()) return p; }
    if (name == L"Gastly") { auto p=bySuffix(assets,{"/goosu.bch"}); if(!p.empty()) return p; }
    if (name == L"Machop") { auto p=bySuffix(assets,{"/wanrikii.bch"}); if(!p.empty()) return p; }
    return requested;
}

float yawToward(float x, float z, float tx, float tz) {
    return std::atan2(tx-x, -(tz-z)) * 180.0f / kPi;
}

void drawFlatBar(float x,float y,float w,float h,float ratio,float r,float g,float b) {
    ratio = std::clamp(ratio,0.0f,1.0f);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(.035f,.045f,.045f,.88f);
    glBegin(GL_QUADS);
    glVertex2f(x,y); glVertex2f(x+w,y); glVertex2f(x+w,y+h); glVertex2f(x,y+h);
    glEnd();
    glColor4f(r,g,b,1);
    glBegin(GL_QUADS);
    glVertex2f(x+2,y+2); glVertex2f(x+2+(w-4)*ratio,y+2);
    glVertex2f(x+2+(w-4)*ratio,y+h-2); glVertex2f(x+2,y+h-2);
    glEnd();
}

} // namespace

PrototypeRomRenderer::~PrototypeRomRenderer(){ destroy(); }

bool PrototypeRomRenderer::create(HWND parent, storage::RomAssetCache* assets) {
    destroy();
    parent_=parent; archive_=assets;
    if(!archive_ || !archive_->isOpen()) return false;

    static bool registered=false;
    if(!registered){
        WNDCLASSW wc{};
        wc.style=CS_OWNDC|CS_HREDRAW|CS_VREDRAW;
        wc.lpfnWndProc=WindowProc;
        wc.hInstance=GetModuleHandleW(nullptr);
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        wc.lpszClassName=L"PromptemonGameSurface";
        if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return false;
        registered=true;
    }
    window_=CreateWindowExW(0,L"PromptemonGameSurface",L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|WS_CLIPCHILDREN,
                            0,0,1,1,parent,nullptr,GetModuleHandleW(nullptr),this);
    if(!window_ || !initializeOpenGL()) return false;

    initializeFonts();
    // Strict by design: this renderer is not allowed to substitute a Windows
    // font or flat placeholder UI. All three native pieces must be decoded.
    if(!uiSkin_.nativeSkinReady()){ destroy(); return false; }
    lastTick_=std::chrono::steady_clock::now();
    return true;
}

void PrototypeRomRenderer::destroy(){
    if(gl_){
        wglMakeCurrent(dc_,gl_);
        releaseStageComposition(); releaseVisual(avatar_); releaseVisual(player_);
        releaseVisual(wild_); releaseVisual(third_); releaseVisual(effect_); releaseFonts();
    }
    shutdownOpenGL();
    if(window_ && IsWindow(window_)) DestroyWindow(window_);
    window_=nullptr; parent_=nullptr; archive_=nullptr;
}

void PrototypeRomRenderer::show(bool visible){ if(window_) ShowWindow(window_,visible?SW_SHOW:SW_HIDE); }
void PrototypeRomRenderer::move(int x,int y,int width,int height){ if(window_) MoveWindow(window_,x,y,std::max(1,width),std::max(1,height),TRUE); }

bool PrototypeRomRenderer::initializeOpenGL(){
    dc_=GetDC(window_); if(!dc_) return false;
    PIXELFORMATDESCRIPTOR p{}; p.nSize=sizeof(p); p.nVersion=1;
    p.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER; p.iPixelType=PFD_TYPE_RGBA;
    p.cColorBits=32; p.cDepthBits=24; p.iLayerType=PFD_MAIN_PLANE;
    const int f=ChoosePixelFormat(dc_,&p);
    if(!f || !SetPixelFormat(dc_,f,&p)) return false;
    gl_=wglCreateContext(dc_); return gl_ && wglMakeCurrent(dc_,gl_)==TRUE;
}

void PrototypeRomRenderer::shutdownOpenGL(){
    if(gl_){ wglMakeCurrent(nullptr,nullptr); wglDeleteContext(gl_); gl_=nullptr; }
    if(dc_ && window_){ ReleaseDC(window_,dc_); dc_=nullptr; }
}

void PrototypeRomRenderer::initializeFonts(){ if(archive_&&gl_){ wglMakeCurrent(dc_,gl_); uiSkin_.load(*archive_); } }
void PrototypeRomRenderer::releaseFonts(){ uiSkin_.release(); }

std::vector<std::uint8_t> PrototypeRomRenderer::animationForModel(const std::string& modelPath) const {
    if(!archive_) return {};
    const std::string p=lowerAscii(modelPath);
    if(p.find("mii/")!=std::string::npos){
        for(const auto& resource:archive_->resources()){
            const auto q=lowerAscii(resource);
            if(endsWith(q,"/mii_anim.bch")||endsWith(q,"/mii_anim.bch.cx")){
                try{return archive_->extractBch(resource);}catch(...){}
            }
        }
    }
    const std::string stem=lowerAscii(std::filesystem::path(modelPath).stem().string());
    for(const auto& resource:archive_->resources()){
        const auto q=lowerAscii(resource);
        if(q==p || q.find(stem)==std::string::npos) continue;
        if(q.find("anim")==std::string::npos && q.find("motion")==std::string::npos) continue;
        try{return archive_->extractBch(resource);}catch(...){}
    }
    return {};
}

bool PrototypeRomRenderer::ensureVisual(Visual& target,const std::string& modelPath,bool loadAnimations){
    if(modelPath.empty()) return false;
    if(target.scene && target.modelPath==modelPath) return true;
    return loadVisual(target,modelPath,loadAnimations);
}

bool PrototypeRomRenderer::loadVisual(Visual& target,const std::string& modelPath,bool loadAnimations){
    if(!archive_||modelPath.empty()) return false;
    wglMakeCurrent(dc_,gl_); releaseVisual(target);
    try{
        const auto model=archive_->extractBch(modelPath);
        const auto motion=loadAnimations?animationForModel(modelPath):std::vector<std::uint8_t>{};
        auto scene=std::make_unique<bch::Scene>(motion.empty()?bch::Scene::parse(model):bch::Scene::parse(model,motion));
        if(scene->meshes().empty()) return false;
        target.modelPath=modelPath; target.scene=std::move(scene); target.frame=0;
        if(loadAnimations){
            target.idle=target.scene->findAnimation("Pii_anim_nomove_kamae");
            if(!target.idle) target.idle=chooseAnimation(target,{"nomove","kamae","idle","wait","stand","fly"},false);
            if(!target.idle) for(const auto& clip:target.scene->animations()) if(clip.loop){target.idle=&clip;break;}
            target.current=target.idle;
        }
        updateBounds(target); uploadTextures(target); return true;
    }catch(...){ releaseVisual(target); return false; }
}

void PrototypeRomRenderer::releaseVisual(Visual& target){
    for(const auto& t:target.textures){ GLuint id=static_cast<GLuint>(t.second); if(id) glDeleteTextures(1,&id); }
    target={};
}

void PrototypeRomRenderer::uploadTextures(Visual& target){
    if(!target.scene) return;
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    for(const auto& texture:target.scene->textures()){
        if(texture.width<=0||texture.height<=0||texture.rgba.empty()) continue;
        GLuint id=0; glGenTextures(1,&id); if(!id) continue;
        glBindTexture(GL_TEXTURE_2D,id);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,texture.width,texture.height,0,GL_RGBA,GL_UNSIGNED_BYTE,texture.rgba.data());
        target.textures[texture.name]=id;
    }
    glBindTexture(GL_TEXTURE_2D,0);
}

void PrototypeRomRenderer::updateBounds(Visual& target){
    if(!target.scene) return;
    bch::Vec3 mn{FLT_MAX,FLT_MAX,FLT_MAX},mx{-FLT_MAX,-FLT_MAX,-FLT_MAX}; bool found=false;
    const auto pose=target.scene->samplePose(nullptr,0);
    for(const auto& mesh:target.scene->meshes()) for(const auto& vertex:mesh.vertices){
        const auto p=target.scene->skin(vertex,pose);
        mn.x=std::min(mn.x,p.x);mn.y=std::min(mn.y,p.y);mn.z=std::min(mn.z,p.z);
        mx.x=std::max(mx.x,p.x);mx.y=std::max(mx.y,p.y);mx.z=std::max(mx.z,p.z);found=true;
    }
    if(!found) return;
    target.boundsMin=mn;target.boundsMax=mx;target.center={(mn.x+mx.x)*.5f,(mn.y+mx.y)*.5f,(mn.z+mx.z)*.5f};
    const float extent=std::max({mx.x-mn.x,mx.y-mn.y,mx.z-mn.z});
    if(extent>1e-5f) target.fitScale=1.52f/extent;
}

void PrototypeRomRenderer::releaseStageComposition(){
    for(auto& p:stageParts_) releaseVisual(p);
    stageParts_.clear();stageDirectory_.clear();stageBoundsMin_={};stageBoundsMax_={};
}

bool PrototypeRomRenderer::loadStageComposition(const std::string& requested){
    releaseStageComposition(); if(!archive_) return false;
    std::string ground=requested;
    if(ground.empty()) ground=fieldStage(archive_);
    if(ground.empty()) return false;
    stageDirectory_=parentVirtualPath(lowerAscii(ground));
    std::set<std::string> candidates; candidates.insert(ground);
    for(const auto& resource:archive_->modelResources()){
        const std::string p=lowerAscii(resource);
        if(!stageDirectory_.empty() && p.rfind(stageDirectory_,0)==0 && !collisionLike(resource)){
            candidates.insert(resource);
            if(candidates.size()>=64) break;
        }
    }
    for(const auto& resource:candidates){ Visual part; if(loadVisual(part,resource,false)) stageParts_.push_back(std::move(part)); }
    if(stageParts_.empty()) return false;
    stagePath_=ground; updateStageCompositionBounds(); return true;
}

void PrototypeRomRenderer::updateStageCompositionBounds(){
    if(stageParts_.empty()) return;
    bch::Vec3 mn{FLT_MAX,FLT_MAX,FLT_MAX},mx{-FLT_MAX,-FLT_MAX,-FLT_MAX};
    for(const auto& p:stageParts_){
        mn.x=std::min(mn.x,p.boundsMin.x);mn.y=std::min(mn.y,p.boundsMin.y);mn.z=std::min(mn.z,p.boundsMin.z);
        mx.x=std::max(mx.x,p.boundsMax.x);mx.y=std::max(mx.y,p.boundsMax.y);mx.z=std::max(mx.z,p.boundsMax.z);
    }
    stageBoundsMin_=mn;stageBoundsMax_=mx;
}

void PrototypeRomRenderer::drawStageComposition(){
    if(stageParts_.empty()) return;
    const float sx=std::max(.001f,stageBoundsMax_.x-stageBoundsMin_.x);
    const float sz=std::max(.001f,stageBoundsMax_.z-stageBoundsMin_.z);
    const float cx=(stageBoundsMin_.x+stageBoundsMax_.x)*.5f;
    const float cz=(stageBoundsMin_.z+stageBoundsMax_.z)*.5f;
    const float fy=stageBoundsMin_.y;
    const bool field=page_==GamePage::Hunt||page_==GamePage::Battle;
    const float scale=std::min((field?20.5f:18.0f)/sx,(field?15.0f:12.5f)/sz);
    glPushMatrix();glScalef(scale,scale,scale);glTranslatef(-cx,-fy,-cz);
    for(auto& p:stageParts_) if(p.scene) drawMeshes(*p.scene,p.textures,nullptr,0);
    glPopMatrix();
}

const bch::Animation* PrototypeRomRenderer::chooseAnimation(Visual& visual,const std::vector<std::string>& tokens,bool preferNonLoop)const{
    if(!visual.scene) return nullptr;
    const bch::Animation* best=nullptr;int bestScore=-99999;
    for(const auto& clip:visual.scene->animations()){
        const std::string n=lowerAscii(clip.name);int score=0;bool matched=false;
        for(const auto& t:tokens) if(n.find(lowerAscii(t))!=std::string::npos){matched=true;score+=100;}
        if(!matched) continue;if(preferNonLoop&&!clip.loop)score+=40;if(preferNonLoop&&clip.loop)score-=20;
        if(clip.frames>5)score+=10;if(score>bestScore){bestScore=score;best=&clip;}
    }
    return best;
}
void PrototypeRomRenderer::playAnimation(Visual& visual,const std::vector<std::string>& tokens,bool preferNonLoop){
    if(const auto* a=chooseAnimation(visual,tokens,preferNonLoop)){visual.current=a;visual.frame=0;}
}

void PrototypeRomRenderer::setStageModel(const std::string& p){stagePath_=p;loadStageComposition(p);}
void PrototypeRomRenderer::setEffectModel(const std::string& p){effectPath_=p;if(!p.empty())ensureVisual(effect_,p,true);}

void PrototypeRomRenderer::showMainMenu(){
    page_=GamePage::MainMenu;
    const auto town=townStage(archive_); if(!town.empty()&&parentVirtualPath(lowerAscii(town))!=stageDirectory_)loadStageComposition(town);
    const auto mii=bySuffix(archive_,{"/mii_normal.bch","/mii_bare.bch"});ensureVisual(avatar_,mii,true);
    ensureVisual(player_,canonical(archive_,{},L"Rattata"),true);
    ensureVisual(wild_,canonical(archive_,{},L"Pidgey"),true);
    ensureVisual(third_,canonical(archive_,{},L"Caterpie"),true);render();
}
void PrototypeRomRenderer::showIntro(){page_=GamePage::Intro;const auto mii=bySuffix(archive_,{"/mii_normal.bch","/mii_bare.bch"});ensureVisual(avatar_,mii,true);render();}
void PrototypeRomRenderer::showOptions(int v,bool f){page_=GamePage::Options;volume_=v;fullscreen_=f;render();}

bool PrototypeRomRenderer::showStarterSelect(const std::string&a,const std::string&b,const std::string&c){
    page_=GamePage::StarterSelect;const auto town=townStage(archive_);if(!town.empty()&&parentVirtualPath(lowerAscii(town))!=stageDirectory_)loadStageComposition(town);
    const auto mii=bySuffix(archive_,{"/mii_normal.bch","/mii_bare.bch"});ensureVisual(avatar_,mii,true);
    const bool x=ensureVisual(player_,canonical(archive_,a,L"Rattata"),true);
    const bool y=ensureVisual(wild_,canonical(archive_,b,L"Pidgey"),true);
    const bool z=ensureVisual(third_,canonical(archive_,c,L"Caterpie"),true);render();return x&&y&&z;
}

bool PrototypeRomRenderer::showHub(const std::string&m,const std::wstring&n,int l,int xp,int wins,int losses,const std::wstring&iv){
    page_=GamePage::Hub;playerName_=n;playerLevel_=l;xp_=xp;wins_=wins;losses_=losses;ivSummary_=iv;
    const auto town=townStage(archive_);if(!town.empty()&&parentVirtualPath(lowerAscii(town))!=stageDirectory_)loadStageComposition(town);
    const auto mii=bySuffix(archive_,{"/mii_normal.bch","/mii_bare.bch"});ensureVisual(avatar_,mii,true);
    const bool ok=ensureVisual(player_,canonical(archive_,m,n),true);render();return ok;
}

bool PrototypeRomRenderer::showHunt(const std::string&m,const std::wstring&n,int l,const std::wstring&iv,bool shiny){
    page_=GamePage::Hunt;wildName_=n;wildLevel_=l;ivReward_=iv;shiny_=shiny;
    const auto field=fieldStage(archive_);if(!field.empty()&&parentVirtualPath(lowerAscii(field))!=stageDirectory_)loadStageComposition(field);
    const bool ok=ensureVisual(wild_,canonical(archive_,m,n),true);render();return ok;
}

bool PrototypeRomRenderer::showBattle(const std::string&pm,const std::string&wm,const std::wstring&pn,int pl,int php,int pmax,
                                      const std::wstring&wn,int wl,int whp,int wmax,const std::wstring&iv,bool shiny,
                                      const std::wstring&m1,const std::wstring&m2,const std::wstring&log,bool over,bool won){
    page_=GamePage::Battle;playerName_=pn;playerLevel_=pl;playerHp_=php;playerMaxHp_=std::max(1,pmax);
    wildName_=wn;wildLevel_=wl;wildHp_=whp;wildMaxHp_=std::max(1,wmax);ivReward_=iv;shiny_=shiny;
    move1_=m1;move2_=m2;battleLog_=log;battleOver_=over;battleWon_=won;
    playerFieldX_=-.35f;playerFieldZ_=2.65f;wildFieldX_=.35f;wildFieldZ_=-1.55f;
    const bool a=ensureVisual(player_,canonical(archive_,pm,pn),true),b=ensureVisual(wild_,canonical(archive_,wm,wn),true);
    render();return a&&b;
}

void PrototypeRomRenderer::playPlayerAction(){playAnimation(player_,{"waza","attack","rush","move"},true);effectTargetsWild_=true;effectTimer_=.78f;}
void PrototypeRomRenderer::playWildAction(){playAnimation(wild_,{"waza","attack","rush","move"},true);effectTargetsWild_=false;effectTimer_=.78f;}
void PrototypeRomRenderer::playPlayerHit(){playAnimation(player_,{"damage","hit","yarare"},true);}
void PrototypeRomRenderer::playWildHit(){playAnimation(wild_,{"damage","hit","yarare"},true);}
void PrototypeRomRenderer::playPlayerFaint(){playAnimation(player_,{"down","dead","faint"},true);}
void PrototypeRomRenderer::playWildFaint(){playAnimation(wild_,{"down","dead","faint"},true);}

void PrototypeRomRenderer::setupPerspective(int w,int h){
    glViewport(0,0,std::max(1,w),std::max(1,h));glMatrixMode(GL_PROJECTION);glLoadIdentity();
    const double aspect=static_cast<double>(std::max(1,w))/std::max(1,h);const double top=.305;
    glFrustum(-top*aspect,top*aspect,-top,top,.55,300);glMatrixMode(GL_MODELVIEW);glLoadIdentity();
}
void PrototypeRomRenderer::setupOverlay(int w,int h){glViewport(0,0,std::max(1,w),std::max(1,h));glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,w,h,0,-1,1);glMatrixMode(GL_MODELVIEW);glLoadIdentity();}
bool PrototypeRomRenderer::playerInAttackRange()const{return page_==GamePage::Battle&&!battleOver_;}
void PrototypeRomRenderer::updateBattleInput(float){
    if(!playerInAttackRange())return;const bool a=(GetAsyncKeyState('Z')&0x8000)||(GetAsyncKeyState(VK_SPACE)&0x8000),b=(GetAsyncKeyState('X')&0x8000);
    if(a&&!attack1Latch_&&parent_&&effectTimer_<=0)SendMessageW(parent_,WM_COMMAND,MAKEWPARAM(static_cast<int>(GameAction::Move1),BN_CLICKED),0);
    if(b&&!attack2Latch_&&parent_&&effectTimer_<=0)SendMessageW(parent_,WM_COMMAND,MAKEWPARAM(static_cast<int>(GameAction::Move2),BN_CLICKED),0);
    attack1Latch_=a;attack2Latch_=b;
}

void PrototypeRomRenderer::drawVisual(Visual&v,float x,float y,float z,float scale,float yaw){
    if(!v.scene)return;glPushMatrix();glTranslatef(x,y,z);glRotatef(yaw,0,1,0);const float f=v.fitScale*scale;glScalef(f,f,f);
    glTranslatef(-v.center.x,-v.center.y,-v.center.z);drawMeshes(*v.scene,v.textures,v.current,v.frame);glPopMatrix();
}
void PrototypeRomRenderer::drawShadow(float x,float y,float z,float rx,float rz,float alpha){
    glDisable(GL_TEXTURE_2D);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glColor4f(.02f,.02f,.02f,alpha);
    glBegin(GL_TRIANGLE_FAN);glVertex3f(x,y,z);for(int i=0;i<=24;++i){const float t=i/24.0f*2*kPi;glVertex3f(x+std::cos(t)*rx,y,z+std::sin(t)*rz);}glEnd();
}
void PrototypeRomRenderer::drawFallbackEffect(float x,float y,float z,float p){
    glDisable(GL_TEXTURE_2D);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);const float q=std::sin(std::clamp(p,0.0f,1.0f)*kPi);
    glColor4f(1,.86f,.2f,1-p);glBegin(GL_LINES);for(int i=0;i<12;++i){const float a=i/12.0f*2*kPi;glVertex3f(x,y,z);glVertex3f(x+std::cos(a)*(.12f+q*.65f),y+.24f,z+std::sin(a)*(.12f+q*.65f));}glEnd();
}
void PrototypeRomRenderer::drawMenuBattleEffect(float x,float y,float z,float p){drawFallbackEffect(x,y,z,p);}
void PrototypeRomRenderer::drawAttackEffect(){if(effectTimer_<=0)return;const float p=1-effectTimer_/.78f,x=effectTargetsWild_?wildFieldX_:playerFieldX_,z=effectTargetsWild_?wildFieldZ_:playerFieldZ_;if(effect_.scene)drawVisual(effect_,x,.45f,z,.36f,0);else drawFallbackEffect(x,.45f,z,p);}

void PrototypeRomRenderer::renderWorld(int w,int h){
    setupPerspective(w,h);glEnable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    const bool field=page_==GamePage::Hunt||page_==GamePage::Battle;
    glPushMatrix();
    // Rumble World uses a stable camera. Town is lower and more frontal; stages
    // are elevated so the toy figures read clearly against the floor.
    glTranslatef(0.0f,field?-2.70f:-2.35f,field?-15.25f:-15.85f);
    glRotatef(field?59.0f:37.0f,1,0,0);
    glRotatef(0.0f,0,1,0);
    drawStageComposition();

    if(page_==GamePage::MainMenu){
        drawShadow(0,.03f,2.05f,.34f,.20f,.22f);drawVisual(player_,0,.40f,2.05f,.46f,180);
        drawShadow(-1.55f,.03f,.25f,.32f,.19f,.18f);drawVisual(avatar_,-1.55f,.43f,.25f,.58f,25);
        drawShadow(1.8f,.03f,-.15f,.30f,.18f,.17f);drawVisual(wild_,1.8f,.40f,-.15f,.42f,-25);
        drawShadow(-2.15f,.03f,-1.15f,.30f,.18f,.17f);drawVisual(third_,-2.15f,.40f,-1.15f,.42f,20);
    }else if(page_==GamePage::Intro){
        drawShadow(0,.03f,.75f,.35f,.20f,.22f);drawVisual(avatar_,0,.43f,.75f,.62f,180);
    }else if(page_==GamePage::StarterSelect){
        drawShadow(0,.03f,2.2f,.33f,.20f,.19f);drawVisual(avatar_,0,.43f,2.2f,.58f,180);
        drawShadow(-2.15f,.03f,-.25f,.30f,.18f,.20f);drawVisual(player_,-2.15f,.40f,-.25f,.46f,10);
        drawShadow(0,.03f,-1.05f,.30f,.18f,.20f);drawVisual(wild_,0,.40f,-1.05f,.46f,0);
        drawShadow(2.15f,.03f,-.25f,.30f,.18f,.20f);drawVisual(third_,2.15f,.40f,-.25f,.46f,-10);
    }else if(page_==GamePage::Hub){
        drawShadow(0,.03f,2.15f,.34f,.20f,.22f);drawVisual(player_,0,.40f,2.15f,.48f,180);
        drawShadow(1.65f,.03f,.25f,.30f,.18f,.18f);drawVisual(avatar_,1.65f,.43f,.25f,.58f,-25);
    }else if(page_==GamePage::Hunt){
        drawShadow(0,.03f,2.55f,.34f,.20f,.24f);drawVisual(player_,0,.40f,2.55f,.48f,180);
        drawShadow(.25f,.03f,-1.55f,.34f,.20f,.24f);drawVisual(wild_,.25f,.40f,-1.55f,.48f,0);
    }else if(page_==GamePage::Battle){
        float px=playerFieldX_,pz=playerFieldZ_,wx=wildFieldX_,wz=wildFieldZ_;
        if(effectTimer_>0){const float p=1-effectTimer_/.78f,q=std::sin(p*kPi),dx=wx-px,dz=wz-pz,len=std::max(.001f,std::sqrt(dx*dx+dz*dz));if(effectTargetsWild_){px+=dx/len*q*.95f;pz+=dz/len*q*.95f;}else{wx-=dx/len*q*.95f;wz-=dz/len*q*.95f;}}
        drawShadow(px,.03f,pz,.34f,.20f,.25f);drawShadow(wx,.03f,wz,.34f,.20f,.25f);
        drawVisual(player_,px,.40f,pz,.50f,yawToward(px,pz,wx,wz));drawVisual(wild_,wx,.40f,wz,.50f,yawToward(wx,wz,px,pz));drawAttackEffect();
    }
    glPopMatrix();
}

void PrototypeRomRenderer::drawPanel(float x,float y,float w,float h,float a){uiSkin_.drawPanel(x,y,w,h,a);}
float PrototypeRomRenderer::textWidth(const std::wstring&t,int k)const{return uiSkin_.textWidth(t,k);}
void PrototypeRomRenderer::drawText(float x,float y,const std::wstring&t,int k,bool c){uiSkin_.drawText(x,y,t,k,c);}
void PrototypeRomRenderer::addHitTarget(GameAction a,const RectF&r,bool e){if(e&&a!=GameAction::None)hitTargets_.push_back({a,r});}
void PrototypeRomRenderer::drawButton(const RectF&r,const std::wstring&l,GameAction a,bool e){uiSkin_.drawButton(r.x,r.y,r.w,r.h,e&&a==hoverAction_,e);drawText(r.x+r.w*.5f,r.y+r.h*.64f,l,1,true);addHitTarget(a,r,e);}
void PrototypeRomRenderer::drawHpPanel(const RectF&r,const std::wstring&n,int level,int hp,int maxHp,bool shiny,bool){
    drawPanel(r.x,r.y,r.w,r.h,.90f);const float ratio=std::clamp(static_cast<float>(hp)/std::max(1,maxHp),0.0f,1.0f);
    drawText(r.x+12,r.y+20,(shiny?L"★ ":L"")+n+L"  Nv."+std::to_wstring(level),0,false);
    const float red=ratio<.25f?.95f:.20f,green=ratio<.25f?.20f:(ratio<.50f?.72f:.86f);drawFlatBar(r.x+12,r.y+r.h-16,r.w-24,9,ratio,red,green,.30f);
}
std::wstring PrototypeRomRenderer::cropText(const std::wstring&v,std::size_t m)const{return v.size()<=m?v:(m<4?v.substr(0,m):v.substr(0,m-3)+L"...");}

void PrototypeRomRenderer::renderOverlay(int width,int height){
    setupOverlay(width,height);glDisable(GL_DEPTH_TEST);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);hitTargets_.clear();
    const float W=static_cast<float>(width),H=static_cast<float>(height);
    switch(page_){
        case GamePage::MainMenu:{
            // Keep the 3D town visible. The requested save actions sit in a
            // Rumble-like bottom strip instead of a desktop-style side panel.
            drawPanel(22,18,320,62,.92f);drawText(42,57,L"PROMPTEMON STEIDIUM",1,false);
            const float gap=10,bw=(W-52-gap*4)/5.0f,y=H-66;
            drawButton({16,y,bw,42},L"Nueva partida",GameAction::NewGame,true);
            drawButton({16+(bw+gap),y,bw,42},L"Cargar",GameAction::None,false);
            drawButton({16+(bw+gap)*2,y,bw,42},L"Borrar",GameAction::None,false);
            drawButton({16+(bw+gap)*3,y,bw,42},L"Exportar",GameAction::None,false);
            drawButton({16+(bw+gap)*4,y,bw,42},L"Opciones",GameAction::Options,true);break;
        }
        case GamePage::Intro:{
            drawPanel(36,H-154,W-72,104,.94f);drawText(58,H-118,L"Bienvenido al Reino de los Juguetes Pokemon.",1,false);
            drawText(58,H-91,L"Elige tu primer companero para empezar.",0,false);drawButton({W-220,H-98,150,36},L"Continuar",GameAction::IntroContinue,true);break;
        }
        case GamePage::StarterSelect:{
            drawPanel(W*.5f-225,20,450,50,.92f);drawText(W*.5f,53,L"ELIGE TU PRIMER POKEMON",1,true);
            const float bw=std::min(270.0f,(W-64)/3.0f),gap=10,total=bw*3+gap*2,x=(W-total)*.5f,y=H-64;
            drawButton({x,y,bw,40},L"Rattata · IV Velocidad",GameAction::StarterRattata,true);
            drawButton({x+bw+gap,y,bw,40},L"Pidgey · IV Velocidad",GameAction::StarterPidgey,true);
            drawButton({x+(bw+gap)*2,y,bw,40},L"Caterpie · IV PS",GameAction::StarterCaterpie,true);break;
        }
        case GamePage::Hub:{
            drawPanel(18,H-88,310,64,.90f);drawText(34,H-60,playerName_+L"  Nv."+std::to_wstring(playerLevel_),1,false);
            drawText(34,H-37,L"XP "+std::to_wstring(xp_)+L"/100   "+ivSummary_,0,false);
            drawButton({W-184,H-66,152,42},L"Caza",GameAction::Hunt,true);break;
        }
        case GamePage::Hunt:{
            drawPanel(W*.5f-155,18,310,66,.92f);drawText(W*.5f,47,(shiny_?L"★ ":L"")+wildName_+L"  Nv."+std::to_wstring(wildLevel_),1,true);
            drawText(W*.5f,69,L"Aporta IV: "+ivReward_,0,true);drawButton({W-190,H-66,158,42},L"Combatir",GameAction::StartBattle,true);break;
        }
        case GamePage::Battle:{
            drawHpPanel({18,H-74,std::min(380.0f,W*.42f),54},playerName_,playerLevel_,playerHp_,playerMaxHp_,false,false);
            drawHpPanel({W*.5f-145,18,290,50},wildName_,wildLevel_,wildHp_,wildMaxHp_,shiny_,true);
            drawPanel(W-224,18,206,48,.88f);drawText(W-121,48,L"IV: "+ivReward_,0,true);
            if(!battleOver_){
                const float bw=174,y=H-68;drawButton({W-bw*2-42,y,bw,42},L"A  "+move1_,GameAction::Move1,effectTimer_<=0);
                drawButton({W-bw-24,y,bw,42},L"B  "+move2_,GameAction::Move2,effectTimer_<=0);
                if(!battleLog_.empty()){drawPanel(18,18,std::min(430.0f,W*.38f),48,.84f);drawText(34,48,cropText(battleLog_,46),0,false);}
            }else{
                drawPanel(W*.5f-205,H-128,410,104,.94f);drawText(W*.5f,H-95,battleWon_?L"VICTORIA":L"DERROTA",1,true);
                drawButton({W*.5f-190,H-72,178,36},L"Volver al hub",GameAction::ReturnHub,true);drawButton({W*.5f+12,H-72,178,36},L"Siguiente",GameAction::NextBattle,true);
            }break;
        }
        case GamePage::Options:{
            drawPanel(W*.5f-245,H*.5f-170,490,340,.96f);drawText(W*.5f,H*.5f-124,L"OPCIONES",2,true);
            drawText(W*.5f-132,H*.5f-56,L"Volumen "+std::to_wstring(volume_)+L"%",1,false);
            drawButton({W*.5f-52,H*.5f-38,52,38},L"-",GameAction::VolumeDown,volume_>0);drawButton({W*.5f+14,H*.5f-38,52,38},L"+",GameAction::VolumeUp,volume_<100);
            drawButton({W*.5f-150,H*.5f+42,138,38},L"Completa",GameAction::Fullscreen,!fullscreen_);drawButton({W*.5f+12,H*.5f+42,138,38},L"Ventana",GameAction::Windowed,fullscreen_);
            drawButton({W*.5f-80,H*.5f+112,160,38},L"Volver",GameAction::Back,true);break;
        }
    }
    glEnable(GL_DEPTH_TEST);
}

void PrototypeRomRenderer::render(){
    if(!window_||!gl_)return;wglMakeCurrent(dc_,gl_);RECT r{};GetClientRect(window_,&r);const int w=std::max(1,(int)(r.right-r.left)),h=std::max(1,(int)(r.bottom-r.top));
    glViewport(0,0,w,h);glClearColor(.58f,.82f,.94f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);renderWorld(w,h);renderOverlay(w,h);SwapBuffers(dc_);
}

void PrototypeRomRenderer::tick(){
    if(!gl_)return;const auto now=std::chrono::steady_clock::now();float dt=std::chrono::duration<float>(now-lastTick_).count();lastTick_=now;dt=std::clamp(dt,0.0f,.1f);
    elapsedSeconds_+=dt;effectTimer_=std::max(0.0f,effectTimer_-dt);updateBattleInput(dt);
    auto advance=[dt](Visual&v){if(!v.current)return;v.frame+=dt*30;if(v.current->frames>0&&v.frame>=v.current->frames){if(v.current->loop)v.frame=std::fmod(v.frame,v.current->frames);else{v.current=v.idle;v.frame=0;}}};
    advance(avatar_);advance(player_);advance(wild_);advance(third_);advance(effect_);render();
}

GameAction PrototypeRomRenderer::hitTest(int x,int y)const{for(auto it=hitTargets_.rbegin();it!=hitTargets_.rend();++it){const auto&r=it->second;if(x>=r.x&&x<=r.x+r.w&&y>=r.y&&y<=r.y+r.h)return it->first;}return GameAction::None;}
LRESULT CALLBACK PrototypeRomRenderer::WindowProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    auto*self=reinterpret_cast<PrototypeRomRenderer*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE){auto*c=reinterpret_cast<CREATESTRUCTW*>(lp);self=static_cast<PrototypeRomRenderer*>(c->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    switch(msg){
        case WM_PAINT:{PAINTSTRUCT p{};BeginPaint(hwnd,&p);if(self)self->render();EndPaint(hwnd,&p);return 0;}
        case WM_ERASEBKGND:return 1;
        case WM_SIZE:if(self)self->render();return 0;
        case WM_MOUSEMOVE:if(self){const auto a=self->hitTest(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));if(a!=self->hoverAction_){self->hoverAction_=a;self->render();}}return 0;
        case WM_LBUTTONUP:if(self&&self->parent_){const auto a=self->hitTest(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));if(a!=GameAction::None)SendMessageW(self->parent_,WM_COMMAND,MAKEWPARAM(static_cast<int>(a),BN_CLICKED),0);}return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

} // namespace promptemon::demo
