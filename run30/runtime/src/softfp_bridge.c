#include "softfp_bridge.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static void (*host_glBlendColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*host_glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*host_glClearDepthf)(GLfloat);
static void (*host_glDepthRangef)(GLfloat, GLfloat);
static void (*host_glLineWidth)(GLfloat);
static void (*host_glPolygonOffset)(GLfloat, GLfloat);
static void (*host_glSampleCoverage)(GLfloat, GLboolean);
static void (*host_glTexParameterf)(GLenum, GLenum, GLfloat);
static void (*host_glUniform1f)(GLint, GLfloat);
static void (*host_glUniform2f)(GLint, GLfloat, GLfloat);
static void (*host_glUniform3f)(GLint, GLfloat, GLfloat, GLfloat);
static void (*host_glUniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
static void (*host_glVertexAttrib1f)(GLuint, GLfloat);
static void (*host_glVertexAttrib2f)(GLuint, GLfloat, GLfloat);
static void (*host_glVertexAttrib3f)(GLuint, GLfloat, GLfloat, GLfloat);
static void (*host_glVertexAttrib4f)(GLuint, GLfloat, GLfloat, GLfloat,
                                     GLfloat);

static int bind_gles_function(nfsmw_gles_resolver resolver, const char *name,
                              void *destination, size_t destination_size)
{
    void *address = resolver(name);

    if (address == NULL || destination_size != sizeof(address)) {
        return -1;
    }
    (void)memcpy(destination, &address, sizeof(address));
    return 0;
}

#define BIND_GLES(member, name)                                             \
    do {                                                                    \
        if (bind_gles_function(resolver, (name), &(member),                  \
                               sizeof(member)) != 0) {                       \
            return -1;                                                      \
        }                                                                   \
    } while (0)

int nfsmw_softfp_bind_gles(nfsmw_gles_resolver resolver)
{
    if (resolver == NULL) {
        return -1;
    }
    BIND_GLES(host_glBlendColor, "glBlendColor");
    BIND_GLES(host_glClearColor, "glClearColor");
    BIND_GLES(host_glClearDepthf, "glClearDepthf");
    BIND_GLES(host_glDepthRangef, "glDepthRangef");
    BIND_GLES(host_glLineWidth, "glLineWidth");
    BIND_GLES(host_glPolygonOffset, "glPolygonOffset");
    BIND_GLES(host_glSampleCoverage, "glSampleCoverage");
    BIND_GLES(host_glTexParameterf, "glTexParameterf");
    BIND_GLES(host_glUniform1f, "glUniform1f");
    BIND_GLES(host_glUniform2f, "glUniform2f");
    BIND_GLES(host_glUniform3f, "glUniform3f");
    BIND_GLES(host_glUniform4f, "glUniform4f");
    BIND_GLES(host_glVertexAttrib1f, "glVertexAttrib1f");
    BIND_GLES(host_glVertexAttrib2f, "glVertexAttrib2f");
    BIND_GLES(host_glVertexAttrib3f, "glVertexAttrib3f");
    BIND_GLES(host_glVertexAttrib4f, "glVertexAttrib4f");
    return 0;
}

NFSMW_SOFTFP void nfsmw_glBlendColor(GLfloat red, GLfloat green,
                                     GLfloat blue, GLfloat alpha)
{
    host_glBlendColor(red, green, blue, alpha);
}

NFSMW_SOFTFP void nfsmw_glClearColor(GLfloat red, GLfloat green,
                                     GLfloat blue, GLfloat alpha)
{
    host_glClearColor(red, green, blue, alpha);
}

NFSMW_SOFTFP void nfsmw_glClearDepthf(GLfloat depth)
{
    host_glClearDepthf(depth);
}

NFSMW_SOFTFP void nfsmw_glDepthRangef(GLfloat near_value, GLfloat far_value)
{
    host_glDepthRangef(near_value, far_value);
}

NFSMW_SOFTFP void nfsmw_glLineWidth(GLfloat width)
{
    host_glLineWidth(width);
}

NFSMW_SOFTFP void nfsmw_glPolygonOffset(GLfloat factor, GLfloat units)
{
    host_glPolygonOffset(factor, units);
}

NFSMW_SOFTFP void nfsmw_glSampleCoverage(GLfloat value, GLboolean invert)
{
    host_glSampleCoverage(value, invert);
}

NFSMW_SOFTFP void nfsmw_glTexParameterf(GLenum target, GLenum name,
                                        GLfloat value)
{
    host_glTexParameterf(target, name, value);
}

NFSMW_SOFTFP void nfsmw_glUniform1f(GLint location, GLfloat x)
{
    host_glUniform1f(location, x);
}

NFSMW_SOFTFP void nfsmw_glUniform2f(GLint location, GLfloat x, GLfloat y)
{
    host_glUniform2f(location, x, y);
}

NFSMW_SOFTFP void nfsmw_glUniform3f(GLint location, GLfloat x, GLfloat y,
                                    GLfloat z)
{
    host_glUniform3f(location, x, y, z);
}

NFSMW_SOFTFP void nfsmw_glUniform4f(GLint location, GLfloat x, GLfloat y,
                                    GLfloat z, GLfloat w)
{
    host_glUniform4f(location, x, y, z, w);
}

NFSMW_SOFTFP void nfsmw_glVertexAttrib1f(GLuint index, GLfloat x)
{
    host_glVertexAttrib1f(index, x);
}

NFSMW_SOFTFP void nfsmw_glVertexAttrib2f(GLuint index, GLfloat x, GLfloat y)
{
    host_glVertexAttrib2f(index, x, y);
}

NFSMW_SOFTFP void nfsmw_glVertexAttrib3f(GLuint index, GLfloat x, GLfloat y,
                                         GLfloat z)
{
    host_glVertexAttrib3f(index, x, y, z);
}

NFSMW_SOFTFP void nfsmw_glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y,
                                         GLfloat z, GLfloat w)
{
    host_glVertexAttrib4f(index, x, y, z, w);
}

NFSMW_SOFTFP double nfsmw_acos(double value) { return acos(value); }
NFSMW_SOFTFP float nfsmw_acosf(float value) { return acosf(value); }
NFSMW_SOFTFP float nfsmw_asinf(float value) { return asinf(value); }
NFSMW_SOFTFP double nfsmw_atan2(double left, double right)
{
    return atan2(left, right);
}
NFSMW_SOFTFP float nfsmw_atan2f(float left, float right)
{
    return atan2f(left, right);
}
NFSMW_SOFTFP double nfsmw_ceil(double value) { return ceil(value); }
NFSMW_SOFTFP float nfsmw_ceilf(float value) { return ceilf(value); }
NFSMW_SOFTFP double nfsmw_cos(double value) { return cos(value); }
NFSMW_SOFTFP float nfsmw_cosf(float value) { return cosf(value); }
NFSMW_SOFTFP double nfsmw_exp(double value) { return exp(value); }
NFSMW_SOFTFP float nfsmw_expf(float value) { return expf(value); }
NFSMW_SOFTFP double nfsmw_floor(double value) { return floor(value); }
NFSMW_SOFTFP float nfsmw_floorf(float value) { return floorf(value); }
NFSMW_SOFTFP double nfsmw_fmod(double left, double right)
{
    return fmod(left, right);
}
NFSMW_SOFTFP float nfsmw_fmodf(float left, float right)
{
    return fmodf(left, right);
}
NFSMW_SOFTFP double nfsmw_frexp(double value, int *exponent)
{
    return frexp(value, exponent);
}
NFSMW_SOFTFP double nfsmw_ldexp(double value, int exponent)
{
    return ldexp(value, exponent);
}
NFSMW_SOFTFP double nfsmw_log(double value) { return log(value); }
NFSMW_SOFTFP double nfsmw_log10(double value) { return log10(value); }
NFSMW_SOFTFP float nfsmw_log10f(float value) { return log10f(value); }
NFSMW_SOFTFP long nfsmw_lrintf(float value) { return lrintf(value); }
NFSMW_SOFTFP double nfsmw_modf(double value, double *integer_part)
{
    return modf(value, integer_part);
}
NFSMW_SOFTFP double nfsmw_pow(double base, double exponent)
{
    return pow(base, exponent);
}
NFSMW_SOFTFP float nfsmw_powf(float base, float exponent)
{
    return powf(base, exponent);
}
NFSMW_SOFTFP double nfsmw_rint(double value) { return rint(value); }
NFSMW_SOFTFP float nfsmw_roundf(float value) { return roundf(value); }
NFSMW_SOFTFP double nfsmw_sin(double value) { return sin(value); }
NFSMW_SOFTFP float nfsmw_sinf(float value) { return sinf(value); }
NFSMW_SOFTFP double nfsmw_sqrt(double value) { return sqrt(value); }
NFSMW_SOFTFP float nfsmw_sqrtf(float value) { return sqrtf(value); }
NFSMW_SOFTFP double nfsmw_strtod(const char *text, char **end)
{
    return strtod(text, end);
}
NFSMW_SOFTFP double nfsmw_tan(double value) { return tan(value); }
NFSMW_SOFTFP float nfsmw_tanf(float value) { return tanf(value); }
