#ifndef NFSMW_SOFTFP_BRIDGE_H
#define NFSMW_SOFTFP_BRIDGE_H

#include <stdint.h>

typedef uint32_t GLenum;
typedef uint32_t GLuint;
typedef int32_t GLint;
typedef uint8_t GLboolean;
typedef float GLfloat;

#if !defined(__arm__)
#error "The NFS MW softfp bridge must be compiled for 32-bit ARM"
#endif

#define NFSMW_SOFTFP __attribute__((pcs("aapcs")))

typedef void *(*nfsmw_gles_resolver)(const char *name);

int nfsmw_softfp_bind_gles(nfsmw_gles_resolver resolver);

NFSMW_SOFTFP void nfsmw_glBlendColor(GLfloat red, GLfloat green,
                                     GLfloat blue, GLfloat alpha);
NFSMW_SOFTFP void nfsmw_glClearColor(GLfloat red, GLfloat green,
                                     GLfloat blue, GLfloat alpha);
NFSMW_SOFTFP void nfsmw_glClearDepthf(GLfloat depth);
NFSMW_SOFTFP void nfsmw_glDepthRangef(GLfloat near_value, GLfloat far_value);
NFSMW_SOFTFP void nfsmw_glLineWidth(GLfloat width);
NFSMW_SOFTFP void nfsmw_glPolygonOffset(GLfloat factor, GLfloat units);
NFSMW_SOFTFP void nfsmw_glSampleCoverage(GLfloat value, GLboolean invert);
NFSMW_SOFTFP void nfsmw_glTexParameterf(GLenum target, GLenum name,
                                        GLfloat value);
NFSMW_SOFTFP void nfsmw_glUniform1f(GLint location, GLfloat x);
NFSMW_SOFTFP void nfsmw_glUniform2f(GLint location, GLfloat x, GLfloat y);
NFSMW_SOFTFP void nfsmw_glUniform3f(GLint location, GLfloat x, GLfloat y,
                                    GLfloat z);
NFSMW_SOFTFP void nfsmw_glUniform4f(GLint location, GLfloat x, GLfloat y,
                                    GLfloat z, GLfloat w);
NFSMW_SOFTFP void nfsmw_glVertexAttrib1f(GLuint index, GLfloat x);
NFSMW_SOFTFP void nfsmw_glVertexAttrib2f(GLuint index, GLfloat x, GLfloat y);
NFSMW_SOFTFP void nfsmw_glVertexAttrib3f(GLuint index, GLfloat x, GLfloat y,
                                         GLfloat z);
NFSMW_SOFTFP void nfsmw_glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y,
                                         GLfloat z, GLfloat w);

NFSMW_SOFTFP double nfsmw_acos(double value);
NFSMW_SOFTFP float nfsmw_acosf(float value);
NFSMW_SOFTFP float nfsmw_asinf(float value);
NFSMW_SOFTFP double nfsmw_atan2(double left, double right);
NFSMW_SOFTFP float nfsmw_atan2f(float left, float right);
NFSMW_SOFTFP double nfsmw_ceil(double value);
NFSMW_SOFTFP float nfsmw_ceilf(float value);
NFSMW_SOFTFP double nfsmw_cos(double value);
NFSMW_SOFTFP float nfsmw_cosf(float value);
NFSMW_SOFTFP double nfsmw_exp(double value);
NFSMW_SOFTFP float nfsmw_expf(float value);
NFSMW_SOFTFP double nfsmw_floor(double value);
NFSMW_SOFTFP float nfsmw_floorf(float value);
NFSMW_SOFTFP double nfsmw_fmod(double left, double right);
NFSMW_SOFTFP float nfsmw_fmodf(float left, float right);
NFSMW_SOFTFP double nfsmw_frexp(double value, int *exponent);
NFSMW_SOFTFP double nfsmw_ldexp(double value, int exponent);
NFSMW_SOFTFP double nfsmw_log(double value);
NFSMW_SOFTFP double nfsmw_log10(double value);
NFSMW_SOFTFP float nfsmw_log10f(float value);
NFSMW_SOFTFP long nfsmw_lrintf(float value);
NFSMW_SOFTFP double nfsmw_modf(double value, double *integer_part);
NFSMW_SOFTFP double nfsmw_pow(double base, double exponent);
NFSMW_SOFTFP float nfsmw_powf(float base, float exponent);
NFSMW_SOFTFP double nfsmw_rint(double value);
NFSMW_SOFTFP float nfsmw_roundf(float value);
NFSMW_SOFTFP double nfsmw_sin(double value);
NFSMW_SOFTFP float nfsmw_sinf(float value);
NFSMW_SOFTFP double nfsmw_sqrt(double value);
NFSMW_SOFTFP float nfsmw_sqrtf(float value);
NFSMW_SOFTFP double nfsmw_strtod(const char *text, char **end);
NFSMW_SOFTFP double nfsmw_tan(double value);
NFSMW_SOFTFP float nfsmw_tanf(float value);

#endif
