/* File generated with Shader Minifier 1.1.4
 * http://www.ctrl-alt-test.fr
 */
#ifndef HEADER_H_
# define HEADER_H_

const char *header_glsl =
"#version 130\n"
"#define Z(s)textureSize(S[s],0)\n"
"#define T(s,c)texture2D(S[s],(c)/Z(s))\n"
"#define X gl_FragCoord.xy\n"
"uniform sampler2D S[12];"
"uniform float F[32];"
//"vec3 f=vec3(F[0],F[1],F[2]),c=vec3(F[3],F[4],F[5]),e=vec3(0.,3.,0.),s=vec3(F[9],F[10],F[11]);"
"float t=F[0];"
"const vec3 E=vec3(0.,.01,1.);"
"const float PI=3.14159,PI2=PI*2.,rt=.1,zt=.3;";
 

#endif // HEADER_H_
