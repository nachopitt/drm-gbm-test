// gcc -o drmgl Linux_DRM_OpenGLES.c `pkg-config --cflags --libs libdrm` -lgbm -lEGL -lGLESv2 -ldl -lsrv_um -I/usr/include/gbm -lIMGegl -ldrm_omap

/*
 * Copyright (c) 2012 Arvin Schnell <arvin.schnell@gmail.com>
 * Copyright (c) 2012 Rob Clark <rob@ti.com>
 * Copyright (c) 2013 Anand Balagopalakrishnan <anandb@ti.com>
 * Copyright (c) 2017 Miouyouyou <Myy> <myy@miouyouyou.fr>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/* Based on a egl cube test app originally written by Arvin Schnell */
/* This is a port of Miouyouyou's blue screen */
/* DRM + GBM + EGL + OpenGLES test to GNU/Linux for */
/* the BeagleBone Black / TI AM335x based on TI's fork of the egl */
/* cube test app ("kmscube"). */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <gbm.h>

#define GL_GLEXT_PROTOTYPES 1
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <assert.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define MAX_DISPLAYS 	(4)
uint8_t DISP_ID = 0;
uint8_t all_display = 0;
int8_t connector_id = 30;
char* device = "/dev/dri/card0";

static struct {
    EGLDisplay display;
    EGLConfig config;
    EGLContext context;
    EGLSurface surface;
    GLuint program;
    GLint modelviewmatrix, modelviewprojectionmatrix, normalmatrix;
    GLuint vbo;
    GLuint positionsoffset, colorsoffset, normalsoffset;
    GLuint vertex_shader, fragment_shader;
} gl;

static struct {
    struct gbm_device *dev;
    struct gbm_surface *surface;
} gbm;

static struct {
    int fd;
    uint32_t ndisp;
    uint32_t crtc_id[MAX_DISPLAYS];
    uint32_t connector_id[MAX_DISPLAYS];
    uint32_t resource_id;
    uint32_t encoder[MAX_DISPLAYS];
    uint32_t format[MAX_DISPLAYS];
    drmModeModeInfo *mode[MAX_DISPLAYS];
    drmModeConnector *connectors[MAX_DISPLAYS];
} drm;

struct drm_fb {
    struct gbm_bo *bo;
    uint32_t fb_id;
};

static uint32_t drm_fmt_to_gbm_fmt(uint32_t fmt)
{
    switch (fmt) {
        case DRM_FORMAT_XRGB8888:
            return GBM_FORMAT_XRGB8888;
        case DRM_FORMAT_ARGB8888:
            return GBM_FORMAT_ARGB8888;
        case DRM_FORMAT_RGB565:
            return GBM_FORMAT_RGB565;
        default:
            printf("Unsupported DRM format: 0x%x", fmt);
            return GBM_FORMAT_XRGB8888;
    }
}

static bool search_plane_format(uint32_t desired_format, int formats_count, uint32_t* formats)
{
    int i;

    for ( i = 0; i < formats_count; i++)
    {
        if (desired_format == formats[i])
            return true;
    }

    return false;
}

int get_drm_prop_val(int fd, drmModeObjectPropertiesPtr props,
        const char *name, unsigned int *p_val) {
    drmModePropertyPtr p;
    unsigned int i, prop_id = 0; /* Property ID should always be > 0 */

    for (i = 0; !prop_id && i < props->count_props; i++) {
        p = drmModeGetProperty(fd, props->props[i]);
        if (!strcmp(p->name, name)){
            prop_id = p->prop_id;
            break;
        }
        drmModeFreeProperty(p);
    }

    if (!prop_id) {
        printf("Could not find %s property\n", name);
        return(-1);
    }

    drmModeFreeProperty(p);
    *p_val = props->prop_values[i];
    return 0;
}

static bool set_drm_format(void)
{
    /* desired DRM format in order */
    static const uint32_t drm_formats[] = {DRM_FORMAT_XRGB8888, DRM_FORMAT_ARGB8888, DRM_FORMAT_RGB565};
    drmModePlaneRes *plane_res;
    bool found = false;
    int i,k;

    drmSetClientCap(drm.fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

    plane_res  = drmModeGetPlaneResources(drm.fd);

    if (!plane_res) {
        printf("drmModeGetPlaneResources failed: %s\n", strerror(errno));
        drmSetClientCap(drm.fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 0);
        return false;
    }

    /*
     * find the plane connected to crtc_id (the primary plane) and then find the desired pixel format
     * from the plane format list
     */
    for (i = 0; i < plane_res->count_planes; i++)
    {
        drmModePlane *plane = drmModeGetPlane(drm.fd, plane_res->planes[i]);
        drmModeObjectProperties *props;
        unsigned int plane_type;

        if(plane == NULL)
            continue;

        props = drmModeObjectGetProperties(drm.fd, plane->plane_id, DRM_MODE_OBJECT_PLANE);

        if(props == NULL){
            printf("plane (%d) properties not found\n",  plane->plane_id);
            drmModeFreePlane(plane);
            continue;
        }

        if(get_drm_prop_val(drm.fd, props, "type",  &plane_type) < 0)
        {
            printf("plane (%d) type value not found\n",  plane->plane_id);
            drmModeFreeObjectProperties(props);
            drmModeFreePlane(plane);
            continue;
        }

        if (plane_type != DRM_PLANE_TYPE_PRIMARY)
        {
            drmModeFreeObjectProperties(props);
            drmModeFreePlane(plane);
            continue;
        }
        else if (!plane->crtc_id)
        {
            plane->crtc_id = drm.crtc_id[drm.ndisp];
        }

        drmModeFreeObjectProperties(props);

        if (plane->crtc_id == drm.crtc_id[drm.ndisp])
        {
            for (k = 0; k < ARRAY_SIZE(drm_formats); k++)
            {
                if (search_plane_format(drm_formats[k], plane->count_formats, plane->formats))
                {
                    drm.format[drm.ndisp] = drm_formats[k];
                    drmModeFreePlane(plane);
                    drmModeFreePlaneResources(plane_res);
                    drmSetClientCap(drm.fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 0);
                    return true;
                }
            }
        }

        drmModeFreePlane(plane);
    }

    drmModeFreePlaneResources(plane_res);
    drmSetClientCap(drm.fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 0);
    return false;
}

static int init_drm(void)
{
    drmModeRes *resources;
    drmModeConnector *connector = NULL;
    drmModeEncoder *encoder = NULL;
    drmModeCrtc *crtc = NULL;

    int i, j, k;
    uint32_t maxRes, curRes;

    /* Open default dri device */
    drm.fd = open(device, O_RDWR | O_CLOEXEC);
    if (drm.fd < 0) {
        printf("could not open drm device %s\n", device);
        return -1;
    }

    resources = drmModeGetResources(drm.fd);
    if (!resources) {
        printf("drmModeGetResources failed: %s\n", strerror(errno));
        return -1;
    }
    drm.resource_id = (uint32_t) resources;

    /* find a connected connector: */
    for (i = 0; i < resources->count_connectors; i++) {
        connector = drmModeGetConnector(drm.fd, resources->connectors[i]);
        if (connector->connection == DRM_MODE_CONNECTED) {

            /* find the matched encoders */
            for (j=0; j<connector->count_encoders; j++) {
                encoder = drmModeGetEncoder(drm.fd, connector->encoders[j]);

                /* Take the fisrt one, if none is assigned */
                if (!connector->encoder_id)
                {
                    connector->encoder_id = encoder->encoder_id;
                }

                if (encoder->encoder_id == connector->encoder_id)
                {
                    /* find the first valid CRTC if not assigned */
                    if (!encoder->crtc_id)
                    {
                        for (k = 0; k < resources->count_crtcs; ++k) {
                            /* check whether this CRTC works with the encoder */
                            if (!(encoder->possible_crtcs & (1 << k)))
                                continue;

                            encoder->crtc_id = resources->crtcs[k];
                            break;
                        }

                        if (!encoder->crtc_id)
                        {
                            printf("Encoder(%d): no CRTC find!\n", encoder->encoder_id);
                            drmModeFreeEncoder(encoder);
                            encoder = NULL;
                            continue;
                        }
                    }

                    break;
                }

                drmModeFreeEncoder(encoder);
                encoder = NULL;
            }

            if (!encoder) {
                printf("Connector (%d): no encoder!\n", connector->connector_id);
                drmModeFreeConnector(connector);
                continue;
            }

            /* choose the current or first supported mode */
            crtc = drmModeGetCrtc(drm.fd, encoder->crtc_id);
            for (j = 0; j < connector->count_modes; j++)
            {
                if (crtc->mode_valid)
                {
                    if ((connector->modes[j].hdisplay == crtc->width) &&
                            (connector->modes[j].vdisplay == crtc->height))
                    {
                        drm.mode[drm.ndisp] = &connector->modes[j];
                        break;
                    }
                }
                else
                {
                    if ((connector->modes[j].hdisplay == crtc->x) &&
                            (connector->modes[j].vdisplay == crtc->y))
                    {
                        drm.mode[drm.ndisp] = &connector->modes[j];
                        break;
                    }
                }
            }

            if(j >= connector->count_modes)
                drm.mode[drm.ndisp] = &connector->modes[0];

            drm.connector_id[drm.ndisp] = connector->connector_id;

            drm.encoder[drm.ndisp]  = (uint32_t) encoder;
            drm.crtc_id[drm.ndisp] = encoder->crtc_id;
            drm.connectors[drm.ndisp] = connector;

            if (!set_drm_format())
            {
                // Error handling
                printf("No desired pixel format found!\n");
                return -1;
            }

            printf("### Display [%d]: CRTC = %d, Connector = %d, format = 0x%x\n", drm.ndisp, drm.crtc_id[drm.ndisp], drm.connector_id[drm.ndisp], drm.format[drm.ndisp]);
            printf("\tMode chosen [%s] : Clock => %d, Vertical refresh => %d, Type => %d\n", drm.mode[drm.ndisp]->name, drm.mode[drm.ndisp]->clock, drm.mode[drm.ndisp]->vrefresh, drm.mode[drm.ndisp]->type);
            printf("\tHorizontal => %d, %d, %d, %d, %d\n", drm.mode[drm.ndisp]->hdisplay, drm.mode[drm.ndisp]->hsync_start, drm.mode[drm.ndisp]->hsync_end, drm.mode[drm.ndisp]->htotal, drm.mode[drm.ndisp]->hskew);
            printf("\tVertical => %d, %d, %d, %d, %d\n", drm.mode[drm.ndisp]->vdisplay, drm.mode[drm.ndisp]->vsync_start, drm.mode[drm.ndisp]->vsync_end, drm.mode[drm.ndisp]->vtotal, drm.mode[drm.ndisp]->vscan);

            /* If a connector_id is specified, use the corresponding display */
            if ((connector_id != -1) && (connector_id == drm.connector_id[drm.ndisp]))
                DISP_ID = drm.ndisp;

            drm.ndisp++;
        }
        else {
            drmModeFreeConnector(connector);
        }
    }

    if (drm.ndisp == 0) {
        /* we could be fancy and listen for hotplug events and wait for
         * a connector..
         */
        printf("no connected connector!\n");
        return -1;
    }

    return 0;
}

static int init_gbm(void)
{
    gbm.dev = gbm_create_device(drm.fd);

    gbm.surface = gbm_surface_create(gbm.dev,
            drm.mode[DISP_ID]->hdisplay, drm.mode[DISP_ID]->vdisplay,
            drm_fmt_to_gbm_fmt(drm.format[DISP_ID]),
            GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!gbm.surface) {
        printf("failed to create gbm surface\n");
        return -1;
    }

    return 0;
}

static int init_gl(void)
{
    EGLint major, minor, n;
    GLuint vertex_shader, fragment_shader;
    GLint ret;

    static const EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    static const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 1,
        EGL_GREEN_SIZE, 1,
        EGL_BLUE_SIZE, 1,
        EGL_ALPHA_SIZE, 0,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    gl.display = eglGetDisplay(gbm.dev);

    if (gl.display == EGL_NO_DISPLAY) {
        printf("Unable to retrieve a suitable EGL display\n");
        return -1;
    }

    if (!eglInitialize(gl.display, &major, &minor)) {
        printf("failed to initialize\n");
        return -1;
    }

    printf("Using display %p with EGL version %d.%d\n",
            gl.display, major, minor);

    printf("EGL Version \"%s\"\n", eglQueryString(gl.display, EGL_VERSION));
    printf("EGL Vendor \"%s\"\n", eglQueryString(gl.display, EGL_VENDOR));
    printf("EGL Extensions \"%s\"\n", eglQueryString(gl.display, EGL_EXTENSIONS));

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        printf("failed to bind api EGL_OPENGL_ES_API\n");
        return -1;
    }

    if (!eglChooseConfig(gl.display, config_attribs, &gl.config, 1, &n) || n != 1) {
        printf("failed to choose config: %d\n", n);
        return -1;
    }

    gl.context = eglCreateContext(gl.display, gl.config,
            EGL_NO_CONTEXT, context_attribs);
    if (gl.context == NULL) {
        printf("failed to create context\n");
        return -1;
    }

    gl.surface = eglCreateWindowSurface(gl.display, gl.config, gbm.surface, NULL);
    if (gl.surface == EGL_NO_SURFACE) {
        printf("failed to create egl surface\n");
        return -1;
    }

    /* connect the context to the surface */
    eglMakeCurrent(gl.display, gl.surface, gl.surface, gl.context);

    printf("GL Extensions: \"%s\"\n", glGetString(GL_EXTENSIONS));

    return 0;
}


static void exit_gbm(void)
{
    gbm_surface_destroy(gbm.surface);
    gbm_device_destroy(gbm.dev);
    return;
}

static void exit_gl(void)
{
    glDeleteProgram(gl.program);
    glDeleteBuffers(1, &gl.vbo);
    glDeleteShader(gl.fragment_shader);
    glDeleteShader(gl.vertex_shader);
    eglDestroySurface(gl.display, gl.surface);
    eglDestroyContext(gl.display, gl.context);
    eglTerminate(gl.display);
    return;
}

static void exit_drm(void)
{

    drmModeRes *resources;
    int i;

    resources = (drmModeRes *)drm.resource_id;
    for (i = 0; i < resources->count_connectors; i++) {
        drmModeFreeEncoder(drm.encoder[i]);
        drmModeFreeConnector(drm.connectors[i]);
    }
    drmModeFreeResources(drm.resource_id);
    close(drm.fd);
    return;
}

void cleanup(void)
{
    exit_gl();
    exit_gbm();
    exit_drm();
    printf("Cleanup of GL, GBM and DRM completed\n");
    return;
}

/* Draw code here */
static void draw(uint32_t i)
{
    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(0.2f, 0.3f, 0.5f, 1.0f);
}

    static void
drm_fb_destroy_callback(struct gbm_bo *bo, void *data)
{
    struct drm_fb *fb = data;
    struct gbm_device *gbm = gbm_bo_get_device(bo);

    if (fb->fb_id)
        drmModeRmFB(drm.fd, fb->fb_id);

    free(fb);
}

static struct drm_fb * drm_fb_get_from_bo(struct gbm_bo *bo)
{
    struct drm_fb *fb = gbm_bo_get_user_data(bo);
    uint32_t width, height, format;
    uint32_t bo_handles[4] = {0}, offsets[4] = {0}, pitches[4] = {0};
    int ret;

    if (fb)
        return fb;

    fb = calloc(1, sizeof *fb);
    fb->bo = bo;

    width = gbm_bo_get_width(bo);
    height = gbm_bo_get_height(bo);
    pitches[0] = gbm_bo_get_stride(bo);
    bo_handles[0] = gbm_bo_get_handle(bo).u32;
    format = gbm_bo_get_format(bo);

    ret = drmModeAddFB2(drm.fd, width, height, format, bo_handles, pitches, offsets, &fb->fb_id, 0);
    if (ret) {
        printf("failed to create fb: %s\n", strerror(errno));
        free(fb);
        return NULL;
    }

    gbm_bo_set_user_data(bo, fb, drm_fb_destroy_callback);

    return fb;
}

static void page_flip_handler(int fd, unsigned int frame,
        unsigned int sec, unsigned int usec, void *data)
{
    int *waiting_for_flip = data;
    *waiting_for_flip = 0;
}

int main(int argc, char *argv[])
{
    fd_set fds;
    drmEventContext evctx = {
        .version = DRM_EVENT_CONTEXT_VERSION,
        .page_flip_handler = page_flip_handler,
    };
    struct gbm_bo *bo;
    struct drm_fb *fb;
    uint32_t i = 0;
    int ret;

    ret = init_drm();
    if (ret) {
        printf("failed to initialize DRM\n");
        return ret;
    }
    printf("### Primary display => ConnectorId = %d, Resolution = %dx%d\n",
            drm.connector_id[DISP_ID], drm.mode[DISP_ID]->hdisplay,
            drm.mode[DISP_ID]->vdisplay);

    FD_ZERO(&fds);
    FD_SET(0, &fds);
    FD_SET(drm.fd, &fds);

    ret = init_gbm();
    if (ret) {
        printf("failed to initialize GBM\n");
        return ret;
    }

    ret = init_gl();
    if (ret) {
        printf("failed to initialize EGL\n");
        return ret;
    }

    /* clear the color buffer */
    glClearColor(0.5, 0.5, 0.5, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    eglSwapBuffers(gl.display, gl.surface);
    bo = gbm_surface_lock_front_buffer(gbm.surface);
    fb = drm_fb_get_from_bo(bo);

    /* set mode: */
    ret = drmModeSetCrtc(drm.fd, drm.crtc_id[DISP_ID], fb->fb_id, 0, 0,
            &drm.connector_id[DISP_ID], 1, drm.mode[DISP_ID]);
    if (ret) {
        printf("failed to set mode: %s\n", strerror(errno));
        return ret;
    }

    while (true) {
        struct gbm_bo *next_bo;
        int waiting_for_flip = 1;

        draw(i++);

        eglSwapBuffers(gl.display, gl.surface);
        next_bo = gbm_surface_lock_front_buffer(gbm.surface);
        fb = drm_fb_get_from_bo(next_bo);

        /*
         * Here you could also update drm plane layers if you want
         * hw composition
         */

        ret = drmModePageFlip(drm.fd, drm.crtc_id[DISP_ID], fb->fb_id,
                DRM_MODE_PAGE_FLIP_EVENT, &waiting_for_flip);
        if (ret) {
            printf("failed to queue page flip: %s\n", strerror(errno));
            return -1;
        }

        while (waiting_for_flip) {
            ret = select(drm.fd + 1, &fds, NULL, NULL, NULL);
            if (ret < 0) {
                printf("select err: %s\n", strerror(errno));
                return ret;
            }
            else if (ret == 0) {
                printf("select timeout!\n");
                return -1;
            }
            else if (FD_ISSET(0, &fds)) {
                printf("user interrupted!\n");
                break;
            }
            drmHandleEvent(drm.fd, &evctx);
        }

        /* release last buffer to render on again: */
        gbm_surface_release_buffer(gbm.surface, bo);
        bo = next_bo;
    }

    cleanup();
    printf("\n Exiting \n");

    return ret;
}
