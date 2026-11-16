// SPDX-License-Identifier: MIT
// Native BGLView presentation cost and complete background coverage.
#include <Application.h>
#include <Bitmap.h>
#include <GLView.h>
#include <Message.h>
#include <Messenger.h>
#include <Roster.h>
#include <Screen.h>
#include <Window.h>
#include <image.h>
#include <atomic>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require(bool condition, const char* what)
{
    if (!condition) {
        fprintf(stderr, "GLVIEW_PRESENT_FAIL %s\n", what);
        exit(1);
    }
}

static constexpr uint32 kCapture = 'Gcpt';
static const rgb_color kLow = {17,93,147,255};

class PresentView : public BGLView {
public:
    PresentView(unsigned width, unsigned height)
        : BGLView(BRect(16,32,width+15,height+31), "GL presentation",
            B_FOLLOW_NONE, B_WILL_DRAW | B_FRAME_EVENTS,
            BGL_RGB | BGL_DOUBLE | BGL_ALPHA),
          fDrawSem(create_sem(0,"GL draw completion")), fWaiting(false), fDraws(0),
          fWidth(width), fHeight(height)
    {
        require(fDrawSem>=0,"draw semaphore");
        SetLowColor(kLow);
        const char* mode=getenv("PROBE_VIEW_COLOR");
        if (mode && !strcmp(mode,"transparent")) SetViewColor(B_TRANSPARENT_COLOR);
        else if (mode && !strcmp(mode,"opaque")) SetViewColor(216,216,216,255);
        else require(!mode,"PROBE_VIEW_COLOR must be opaque or transparent");
        rgb_color color=ViewColor();
        printf("GLVIEW_COLOR %u,%u,%u,%u transparent=%d\n",color.red,
            color.green,color.blue,color.alpha,color==B_TRANSPARENT_COLOR);
        const char* expected=getenv("PROBE_EXPECT_TRANSPARENT");
        if (expected) require((color==B_TRANSPARENT_COLOR)==(atoi(expected)!=0),
            "default view color");
    }
    ~PresentView() { delete_sem(fDrawSem); }
    void Draw(BRect update) override
    {
        BGLView::Draw(update);
        // Complete the bitmap copy before the next swap reuses its storage.
        Sync();
        ++fDraws;
        if (fWaiting.exchange(false)) release_sem(fDrawSem);
    }
    void Render(unsigned frame)
    {
        // No old update can satisfy this frame's completion: the window
        // cannot draw between setting fWaiting and publishing the bitmap.
        require(Window()->Lock(),"render window lock");
        LockGL();
        glViewport(0,0,Bounds().IntegerWidth()+1,Bounds().IntegerHeight()+1);
        glDisable(GL_DITHER); glDisable(GL_SCISSOR_TEST);
        unsigned color=frame&7;
        glClearColor(!!(color&1),!!(color&2),!!(color&4),1);
        glClear(GL_COLOR_BUFFER_BIT);
        color=(frame+3)&7;
        glEnable(GL_SCISSOR_TEST); glScissor(5,7,31,23);
        glClearColor(!!(color&1),!!(color&2),!!(color&4),1);
        glClear(GL_COLOR_BUFFER_BIT); glDisable(GL_SCISSOR_TEST);
        require(glGetError()==GL_NO_ERROR,"render");
        fWaiting=true;
        SwapBuffers();
        UnlockGL();
        Window()->Unlock();
        status_t status;
        do {
            status=acquire_sem_etc(fDrawSem,1,B_RELATIVE_TIMEOUT,5000000);
        } while(status==B_INTERRUPTED);
        require(status==B_OK,"frame draw completion");
    }
    void Resize(unsigned width,unsigned height)
    {
        require(Window()->Lock(),"resize lock");
        ResizeTo(width-1,height-1);
        Window()->Unlock();
        bigtime_t deadline=system_time()+5000000;
        while(fWidth.load()!=width || fHeight.load()!=height) {
            require(system_time()<deadline,"resize callback"); snooze(1000);
        }
    }
    void FrameResized(float width,float height) override
    {
        BGLView::FrameResized(width,height);
        fWidth=unsigned(width)+1; fHeight=unsigned(height)+1;
    }
    void Verify(int frame,unsigned bitmapWidth,unsigned bitmapHeight)
    {
        BMessage request(kCapture),reply;
        request.AddInt32("frame",frame);
        request.AddInt32("width",bitmapWidth);
        request.AddInt32("height",bitmapHeight);
        require(BMessenger(this).SendMessage(&request,&reply,5000000,5000000)==B_OK,
            "screen check message");
        require(reply.GetBool("passed",false),"screen check");
    }
    void MessageReceived(BMessage* message) override
    {
        if (message->what!=kCapture) { BGLView::MessageReceived(message); return; }
        Invalidate(); Window()->UpdateIfNeeded(); Window()->Sync();
        BRect bounds=Bounds(),screenBounds=bounds;
        ConvertToScreen(&screenBounds);
        BBitmap copy(bounds,0,B_RGB32);
        BScreen screen(Window());
        require(copy.InitCheck()==B_OK && screen.IsValid(),"screen bitmap");
        require(screen.ReadBitmap(&copy,false,&screenBounds)==B_OK,"screen capture");
        int frame=message->GetInt32("frame",-1);
        unsigned width=message->GetInt32("width",0),height=message->GetInt32("height",0);
        bool noRenderer=getenv("PROBE_NO_RENDERER")!=nullptr;
        font_height fontHeight; GetFontHeight(&fontHeight);
        BRect textBounds(7,31-fontHeight.ascent,
            9+StringWidth("No EGL renderer available!"),33+fontHeight.descent);
        unsigned mismatches=0,textPixels=0;
        for (int y=0;y<=bounds.IntegerHeight();++y) {
            auto row=static_cast<const unsigned char*>(copy.Bits())+y*copy.BytesPerRow();
            for (int x=0;x<=bounds.IntegerWidth();++x) {
                rgb_color expected=kLow;
                if(frame>=0 && unsigned(x)<width && unsigned(y)<height) {
                    unsigned glY=height-1-y;
                    unsigned color=(x>=5 && x<36 && glY>=7 && glY<30)
                        ? ((frame+3)&7) : (frame&7);
                    expected={uint8((color&1)?255:0),uint8((color&2)?255:0),
                        uint8((color&4)?255:0),255};
                }
                if(row[x*4]!=expected.blue || row[x*4+1]!=expected.green
                        || row[x*4+2]!=expected.red) {
                    if(noRenderer && textBounds.Contains(BPoint(x,y))) {
                        ++textPixels; continue;
                    }
                    if(!mismatches) fprintf(stderr,"pixel x=%d y=%d got=%u,%u,%u expected=%u,%u,%u\n",
                        x,y,row[x*4+2],row[x*4+1],row[x*4],expected.red,expected.green,expected.blue);
                    ++mismatches;
                }
            }
        }
        printf("GLVIEW_PIXELS frame=%d bitmap=%ux%u view=%dx%d mismatches=%u error_text_pixels=%u\n",
            frame,width,height,bounds.IntegerWidth()+1,bounds.IntegerHeight()+1,mismatches,textPixels);
        BMessage reply(B_REPLY); reply.AddBool("passed",mismatches==0
            && (!noRenderer || textPixels>0)); message->SendReply(&reply);
    }
    uint64 Draws() const { return fDraws.load(); }
private:
    sem_id fDrawSem;
    std::atomic<bool> fWaiting;
    std::atomic<uint64> fDraws;
    std::atomic<unsigned> fWidth,fHeight;
};

static bigtime_t cpu_time(team_id team)
{
    team_usage_info usage={};
    require(get_team_usage_info(team,B_TEAM_USAGE_SELF,&usage)==B_OK,"team CPU");
    return usage.user_time+usage.kernel_time;
}
static team_id app_server()
{
    team_id team=be_roster->TeamFor("application/x-vnd.vos-app_server");
    require(team>=B_OK,"app_server team"); return team;
}
struct Arguments { bool benchmark; unsigned width,height,frames; };
static int32 worker(void* data)
{
    Arguments args=*static_cast<Arguments*>(data);
    BWindow* window=new BWindow(BRect(16,64,args.width+63,args.height+127),
        "BGLView presentation",B_TITLED_WINDOW,
        B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE | B_NOT_CLOSABLE);
    PresentView* view=new PresentView(args.width,args.height);
    window->AddChild(view); window->Show();
    int32 imageCookie=0; image_info imageInfo;
    while(get_next_image_info(B_CURRENT_TEAM,&imageCookie,&imageInfo)==B_OK)
        if(strstr(imageInfo.name,"/libGL.so."))
            printf("GLVIEW_LIBRARY %s\n",imageInfo.name);
    // Before any SwapBuffers(), Draw must fill the entire view with LowColor.
    view->Verify(-1,0,0);
    require(window->Lock(),"renderer check lock"); view->LockGL();
    const char* renderer=reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    printf("GLVIEW_RENDERER %s\n",renderer?renderer:"none");
    bool noRenderer=getenv("PROBE_NO_RENDERER")!=nullptr;
    require(noRenderer || renderer!=nullptr,"GL renderer");
    view->UnlockGL(); window->Unlock();
    if(!noRenderer) {
        for(unsigned frame=0;frame<4;++frame) view->Render(frame);
        view->Verify(3,args.width,args.height);
        team_id server=app_server();
        bigtime_t clientStart=cpu_time(B_CURRENT_TEAM),serverStart=cpu_time(server);
        uint64 draws=view->Draws(); bigtime_t start=system_time();
        for(unsigned frame=0;frame<args.frames;++frame) {
            view->Render(frame);
            if(!args.benchmark) view->Verify(frame,args.width,args.height);
        }
        require(window->Lock(),"finish lock"); window->Sync(); window->Unlock();
        double seconds=(system_time()-start)/1e6;
        bigtime_t clientCPU=cpu_time(B_CURRENT_TEAM)-clientStart;
        bigtime_t serverCPU=cpu_time(server)-serverStart;
        if(args.benchmark)
            printf("GLVIEW_BENCH width=%u height=%u frames=%u draws=%llu seconds=%.6f draws_per_second=%.3f client_us=%lld app_server_us=%lld\n",
                args.width,args.height,args.frames,(unsigned long long)(view->Draws()-draws),
                seconds,args.frames/seconds,(long long)clientCPU,(long long)serverCPU);
        view->Verify(args.frames-1,args.width,args.height);
        // With a smaller old bitmap, freshly exposed pixels must use LowColor.
        view->Resize(79,47); view->Render(5); view->Verify(5,79,47);
        view->Resize(args.width,args.height); view->Verify(5,79,47);
        view->Render(6); view->Verify(6,args.width,args.height);
    }
    require(window->Lock(),"close lock"); window->Quit();
    ssize_t cookie=0; area_info area; unsigned shared=0;
    while(get_next_area_info(B_CURRENT_TEAM,&cookie,&area)==B_OK)
        if(!strcmp(area.name,"Haiku EGL bitmap")) ++shared;
    require(shared==0,"shared areas retired");
    printf("GLVIEW_PRESENT_PASS shared_areas=0\n");
    be_app->PostMessage(B_QUIT_REQUESTED); return 0;
}
int main(int argc,char** argv)
{
    setvbuf(stdout,nullptr,_IOLBF,0);
    Arguments args={false,513,257,16};
    if(argc==5 && !strcmp(argv[1],"--benchmark")) {
        args={true,unsigned(atoi(argv[2])),unsigned(atoi(argv[3])),unsigned(atoi(argv[4]))};
        require(args.width>=80 && args.width<=1856 && args.height>=64
            && args.height<=900 && args.frames>0 && args.frames<=10000,"benchmark range");
    } else require(argc==1,"arguments");
    BApplication app("application/x-vnd.airOS-rpi4-glview-present-probe");
    require(app.InitCheck()==B_OK,"application"); app.HideCursor();
    thread_id thread=spawn_thread(worker,"GL presentation test",B_NORMAL_PRIORITY,&args);
    require(thread>=0 && resume_thread(thread)==B_OK,"worker"); app.Run();
    status_t result=0; status_t waited=wait_for_thread(thread,&result); app.ShowCursor();
    return waited==B_OK && result==0 ? 0 : 1;
}
