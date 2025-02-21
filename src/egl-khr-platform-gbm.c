// Taken from
// https://registry.khronos.org/EGL/extensions/KHR/EGL_KHR_platform_gbm.txt
//
// Name
//
//     KHR_platform_gbm
//
// Name Strings
//
//     EGL_KHR_platform_gbm
//
// Contributors
//
//     Chad Versace <chad.versace@intel.com>
//     Jon Leech (oddhack 'at' sonic.net)
//     Kristian HÃ¸gsberg <krh@bitplanet.org>
//
// Contacts
//
//     Chad Versace <chad.versace@intel.com>
//
// Status
//
//     Complete.
//     Approved by the EGL Working Group on January 31, 2014.
//     Ratified by the Khronos Board of Promoters on March 14, 2014.
//
// Version
//
//     Version 3, 2016/01/04
//
// Number
//
//     EGL Extension #69
//
// Extension Type
//
//     EGL client extension
//
// Dependencies
//
//     EGL 1.5 is required.
//
//     This extension is written against the EGL 1.5 Specification (draft
//     20140122).
//
// Overview
//
//     This extension defines how to create EGL resources from native GBM
//     resources using the EGL 1.5 platform functionality (GBM is a Generic
//     Buffer Manager for Linux).
//
// New Types
//
//     None
//
// New Procedures and Functions
//
//     None
//
// New Tokens
//
//     Accepted as the <platform> argument of eglGetPlatformDisplay:
//
//         EGL_PLATFORM_GBM_KHR                     0x31D7
//
// Additions to the EGL Specification
//
//     None.
//
// New Behavior
//
//     To determine if the EGL implementation supports this extension, clients
//     should query the EGL_EXTENSIONS string of EGL_NO_DISPLAY.
//
//     To obtain an EGLDisplay from an GBM device, call eglGetPlatformDisplay with
//     <platform> set to EGL_PLATFORM_GBM_KHR. The <native_display> parameter
//     specifies the GBM device to use and must either point to a `struct
//     gbm_device` or be EGL_DEFAULT_DISPLAY. If <native_display> is
//     EGL_DEFAULT_DISPLAY, then the resultant EGLDisplay will be backed by some
//     implementation-chosen GBM device.
//
//     For each EGLConfig that belongs to the GBM platform, the
//     EGL_NATIVE_VISUAL_ID attribute is a GBM color format, such as
//     GBM_FORMAT_XRGB8888.
//
//     To obtain a rendering surface from a GBM surface, call
//     eglCreatePlatformWindowSurface with a <dpy> that belongs to the GBM
//     platform and a <native_window> that points to a `struct gbm_surface`.  If
//     <native_window> was created without the GBM_BO_USE_RENDERING flag, or if
//     the color format of <native_window> differs from the EGL_NATIVE_VISUAL_ID
//     of <config>, then the function fails and generates EGL_BAD_MATCH.
//
//     It is not valid to call eglCreatePlatformPixmapSurface with a <dpy> that
//     belongs to the GBM platform. Any such call fails and generates
//     an EGL_BAD_PARAMETER error.
//
// Issues
//
//     1. Should this extension permit EGL_DEFAULT_DISPLAY as input to
//        eglGetPlatformDisplay?
//
//        RESOLUTION: Yes. When given EGL_DEFAULT_DISPLAY, eglGetPlatformDisplay
//        returns an EGLDisplay backed by an implementation-chosen GBM device.
//
// Example Code

// This example program creates an EGL surface from a GBM surface.
//
// If the macro EGL_KHR_platform_gbm is defined, then the program
// creates the surfaces using the methods defined in this specification.
// Otherwise, it uses the methods defined by the EGL 1.4 specification.
//
// Compile with `cc -std=c99 example.c -lgbm -lEGL`.
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <EGL/egl.h>
#include <gbm.h>

struct my_display {
    struct gbm_device *gbm;
    EGLDisplay egl;
};

struct my_config {
    struct my_display dpy;
    EGLConfig egl;
};

struct my_window {
    struct my_config config;
    struct gbm_surface *gbm;
    EGLSurface egl;
};

    static void
check_extensions(void)
{
#ifdef EGL_KHR_platform_gbm
    const char *client_extensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);

    if (!client_extensions) {
        // No client extensions string available
        abort();
    }
    if (!strstr(client_extensions, "EGL_KHR_platform_gbm")) {
        abort();
    }
#endif
}

    static struct my_display
get_display(void)
{
    struct my_display dpy;

    int fd = open("/dev/dri/card0", O_RDWR | FD_CLOEXEC);
    if (fd < 0) {
        abort();
    }

    dpy.gbm = gbm_create_device(fd);
    if (!dpy.gbm) {
        abort();
    }


#ifdef EGL_KHR_platform_gbm
    dpy.egl = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, dpy.gbm, NULL);
#else
    dpy.egl = eglGetDisplay(dpy.gbm);
#endif

    if (dpy.egl == EGL_NO_DISPLAY) {
        abort();
    }

    EGLint major, minor;
    if (!eglInitialize(dpy.egl, &major, &minor)) {
        abort();
    }

    return dpy;
}

    static struct my_config
get_config(struct my_display dpy)
{
    struct my_config config = {
        .dpy = dpy,
    };

    EGLint egl_config_attribs[] = {
        EGL_BUFFER_SIZE,        32,
        EGL_DEPTH_SIZE,         EGL_DONT_CARE,
        EGL_STENCIL_SIZE,       EGL_DONT_CARE,
        EGL_RENDERABLE_TYPE,    EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE,       EGL_WINDOW_BIT,
        EGL_NONE,
    };

    EGLint num_configs;
    if (!eglGetConfigs(dpy.egl, NULL, 0, &num_configs)) {
        abort();
    }

    EGLConfig *configs = malloc(num_configs * sizeof(EGLConfig));
    if (!eglChooseConfig(dpy.egl, egl_config_attribs,
                configs, num_configs, &num_configs)) {
        abort();
    }
    if (num_configs == 0) {
        abort();
    }

    // Find a config whose native visual ID is the desired GBM format.
    for (int i = 0; i < num_configs; ++i) {
        EGLint gbm_format;

        if (!eglGetConfigAttrib(dpy.egl, configs[i],
                    EGL_NATIVE_VISUAL_ID, &gbm_format)) {
            abort();
        }

        if (gbm_format == GBM_FORMAT_XRGB8888) {
            config.egl = configs[i];
            free(configs);
            return config;
        }
    }

    // Failed to find a config with matching GBM format.
    abort();
}

    static struct my_window
get_window(struct my_config config)
{
    struct my_window window = {
        .config = config,
    };

    window.gbm = gbm_surface_create(config.dpy.gbm,
            256, 256,
            GBM_FORMAT_XRGB8888,
            GBM_BO_USE_RENDERING);
    if (!window.gbm) {
        abort();
    }

#ifdef EGL_KHR_platform_gbm
    window.egl = eglCreatePlatformWindowSurface(config.dpy.egl,
            config.egl,
            window.gbm,
            NULL);
#else
    window.egl = eglCreateWindowSurface(config.dpy.egl,
            config.egl,
            (EGLNativeWindowType)window.gbm,
            NULL);
#endif

    if (window.egl == EGL_NO_SURFACE) {
        abort();
    }

    return window;
}

    int
main(void)
{
    check_extensions();

    struct my_display dpy = get_display();
    struct my_config config = get_config(dpy);
    struct my_window window = get_window(config);

    (void)window;

    return 0;
}
