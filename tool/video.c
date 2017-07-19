#include "video.h"
#include "Automation.h"
#include "fileres.h"

#include "atto/app.h"
#define ATTO_GL_DEBUG
#define ATTO_GL_H_IMPLEMENT
#include "atto/gl.h"

#define GL(F) AGL__CALL(gl##F)

static const char fs_vtx_source[] =
	"void main() {\n"
		"gl_Position = gl_Vertex;\n"
	"}"
;

typedef struct {
	const char *name;
	GLuint fb;
	GLuint program;
	int targets;
	VolatileResource *fragment_source;
} RenderPass;

enum {
	Pass_Raymarch,
	Pass_BlurReflection,
	Pass_BlurReflection2,
	Pass_Composite,
	Pass_DofTap,
	Pass_DofFill,
	Pass_MergeAndPost,
	Pass_ToolOut,
	Pass_MAX
};

enum {
	Texture_Noise,
	Texture_RaymarchPrimary,
	Texture_RaymarchReflection,
	Texture_RaymarchReflectionBlur,
	Texture_RaymarchReflectionBlur2,
	Texture_RaymarchCombined,
	Texture_DofTapNear,
	Texture_DofTapFar,
	Texture_DofFillNear,
	Texture_DofFillFar,
	Texture_Frame,
	Texture_MAX
};

static struct {
	int width, height;
	GLuint textures[Texture_MAX];
	RenderPass pass[Pass_MAX];
	GLint texture_unit[Texture_MAX];
	VolatileResource *common_header;
} g;

void passInit(RenderPass *pass, int ntex, const GLuint *tex, const char *src_file) {
	if (ntex > 0) {
		pass->targets = ntex;
		GL(GenFramebuffers(1, &pass->fb));
		GL(BindFramebuffer(GL_FRAMEBUFFER, pass->fb)); 
		for (int i = 0; i < ntex; ++i) {
			aAppDebugPrintf("%s %p %d %d %d", src_file, pass, pass->fb, i, tex[i]);
			GL(FramebufferTexture2D(GL_FRAMEBUFFER,
					GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, tex[i], 0));
		}
		int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		ATTO_ASSERT(status == GL_FRAMEBUFFER_COMPLETE);
	} else
		pass->fb = 0;

	pass->name = src_file;

	pass->program = 0;
	pass->fragment_source = resourceOpenFile(src_file);
}

int passCheckAndUpdateProgram(RenderPass *pass) {
	if (!pass->fragment_source->updated && !g.common_header->updated)
		return 0;

	aAppDebugPrintf("Loading for %s", pass->name);

	const char * const vertex[] = { fs_vtx_source, NULL };
	const char * const fragment[] = { 
		g.common_header->bytes,
		pass->fragment_source->bytes,
		NULL };
	AGLProgram new_program = aGLProgramCreate(vertex, fragment);

	if (new_program < 0) {
		aAppDebugPrintf("shader error: %s\n", a_gl_error);
		return 0;
	}

	if (pass->program > 0)
		aGLProgramDestroy(pass->program);

	pass->program = new_program;
	return 1;
}

static void drawPass(const Frame *frame, const RenderPass *pass) {
	GL(UseProgram(pass->program));
	GL(BindFramebuffer(GL_FRAMEBUFFER, pass->fb));
	if (pass->fb > 0) {
		GL(Viewport(0, 0, g.width, g.height));
		//GL(Uniform3f(glGetUniformLocation(prog, "V"), g.width, g.height, 0));
		frame->signal[0] = g.width;
		frame->signal[1] = g.height;

		const GLuint bufs[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
		GL(DrawBuffers(pass->targets, bufs));
	} else {
		GL(Viewport(0, 0, a_app_state->width, a_app_state->height));
		//GL(Uniform3f(glGetUniformLocation(prog, "V"), a_app_state->width, a_app_state->height, 0));
		frame->signal[0] = a_app_state->width;
		frame->signal[1] = a_app_state->height;
	}
	//GL(BindFragDataLocation(prog, 0, "C0"));
	//GL(BindFragDataLocation(prog, 1, "C1"));
	GL(Uniform1iv(glGetUniformLocation(pass->program, "S"), Texture_MAX, g.texture_unit));
	GL(Uniform1fv(glGetUniformLocation(pass->program, "F"), frame->end - frame->start, frame->signal));
	//GL(Uniform3f(glGetUniformLocation(prog, "C"), frame->signal[0], frame->signal[1], frame->signal[2]));
	//GL(Uniform3f(glGetUniformLocation(prog, "A"), frame->signal[3], frame->signal[4], frame->signal[5]));
	//GL(Uniform3f(glGetUniformLocation(prog, "D"), frame->signal[6], frame->signal[7], frame->signal[8]));
	//GL(Uniform4f(glGetUniformLocation(prog, "M"), midi_[0], midi_[1], midi_[2], midi_[3]));

	GL(Rects(-1,-1,1,1));
}

#define NOISE_SIZE 256
static uint32_t noise_bytes[NOISE_SIZE * NOISE_SIZE];

void videoInit(int width, int height) {
	g.width = width;
	g.height = height;

	g.common_header = resourceOpenFile("header.glsl");

	GL(GenTextures(Texture_MAX, g.textures));

	{
		uint64_t seed = 0;
		for (int i = 0; i < NOISE_SIZE * NOISE_SIZE; ++i) {
			seed = 1442695040888963407ull + seed * 6364136223846793005ull;
			noise_bytes[i] = (seed >> 32);
		}

		g.texture_unit[0] = 0;
		GL(ActiveTexture(GL_TEXTURE0));
		GL(BindTexture(GL_TEXTURE_2D, g.textures[0]));
		GL(TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, NOISE_SIZE, NOISE_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, noise_bytes));
		GL(TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
		GL(TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
		GL(TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT));
		GL(TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT));
	}

	for (int i = 1; i < Texture_MAX; ++i) {
		g.texture_unit[i] = i;
		GL(ActiveTexture(GL_TEXTURE0 + i));
		GL(BindTexture(GL_TEXTURE_2D, g.textures[i]));
		GL(TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, 0));
		GL(TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
		GL(TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
		GL(TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER));
		GL(TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER));
	}

	{
		GLuint *tex = g.textures + 1;
#define PASS(PASS, NTEX, FILENAME) \
	passInit(g.pass + PASS, NTEX, tex, FILENAME); tex += NTEX
		PASS(Pass_Raymarch, 2, "raymarch.glsl");
		PASS(Pass_BlurReflection, 1, "blur_reflection.glsl");
		PASS(Pass_BlurReflection2, 1, "blur_reflection2.glsl");
		PASS(Pass_Composite, 1, "composite.glsl");
		PASS(Pass_DofTap, 2, "dof_tap.glsl");
		PASS(Pass_DofFill, 2, "dof_fill.glsl");
		PASS(Pass_MergeAndPost, 1, "post.glsl");
		PASS(Pass_ToolOut, 0, "out.glsl");
#undef PASS
	}
}

void videoPaint(const Frame *frame, int force_redraw) {
	int need_redraw = force_redraw;

	for (int i = 0; i < Pass_MAX; ++i)
		need_redraw |= passCheckAndUpdateProgram(g.pass + i);

	if (need_redraw) {
		for (int i = 0; i < Pass_MAX; ++i)
			drawPass(frame, g.pass + i);
	}
}

