#import <UIKit/UIKit.h>
#import <OpenGLES/gl.h>
#import "fishhook.h"

// pointers to original functions
static void (*orig_glViewport)(GLint x, GLint y, GLsizei width, GLsizei height) = NULL;
static void (*orig_glScissor)(GLint x, GLint y, GLsizei width, GLsizei height) = NULL;

// helper to get native screen size in pixels (landscape expected)
static void getNativeScreenSize(int *outW, int *outH) {
    @autoreleasepool {
        UIScreen *s = [UIScreen mainScreen];
        CGSize b = s.bounds.size;
        CGFloat scale = s.scale;
        // Game is landscape - provide landscape width>height
        int w = (int)round(MAX(b.width, b.height) * scale);
        int h = (int)round(MIN(b.width, b.height) * scale);
        *outW = w;
        *outH = h;
    }
}

void hooked_glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    int nw=0, nh=0;
    getNativeScreenSize(&nw, &nh);
    if (nw>0 && nh>0) {
        // Force viewport to native full-screen
        orig_glViewport(0, 0, (GLsizei)nw, (GLsizei)nh);
    } else {
        orig_glViewport(x,y,width,height);
    }
}

void hooked_glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    int nw=0, nh=0;
    getNativeScreenSize(&nw, &nh);
    if (nw>0 && nh>0) {
        orig_glScissor(0, 0, (GLsizei)nw, (GLsizei)nh);
    } else {
        orig_glScissor(x,y,width,height);
    }
}

// Constructor: rebind symbols when dylib loads
__attribute__((constructor))
static void init_hook() {
    // rebind glViewport and glScissor to our implementations
    rebind_symbols((struct rebinding[2]){ { "glViewport", (void*)hooked_glViewport, (void**)&orig_glViewport },
                                           { "glScissor", (void*)hooked_glScissor, (void**)&orig_glScissor } }, 2);
}