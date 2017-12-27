
#include "shared.h"

#define API_EXPORT extern "C" __declspec(dllexport)

#define X(ret, name, ...)	\
API_EXPORT ret (*name)(__VA_ARGS__) = 0;

OPENGL_FUNCTIONS
SYSTEM_FUNCTIONS
#undef X

////////

struct vec2 { f32 x, y; };
struct vec3 { f32 x, y, z; };

struct vec4
{
	union { f32 x, r; };
	union { f32 y, g; };
	union { f32 z, b; };
	union { f32 w, a; };
};

struct rect2d { f32 x0, y0, x1, y1; };

struct vertex
{
	vec3 position;
	vec2 texcoord;
	vec4 color;
};

using vertex_buffer = array<i32, vertex>;

struct app_state
{
	u32 vao;
	u32 vbo;

	u32 basic_program;
	i32 basic_uproj;

	u32 texture_program;
	i32 texture_uproj;
	i32 texture_umap;

	u32 atlas;
	i32 atlas_width;
	i32 atlas_height;
	u32 atlas_is_dirty;

	i32 atlas_ymax;
	i32 atlas_x;
	i32 atlas_y;

	u32 *atlas_bits;

	vertex_buffer vertices;

	font *console_font;
	font *ui_font;

	i32 mouse_x;
	i32 mouse_y;
	u32 mouse_buttons;

	u32 pad;

	// debug
	vec2 debug_cursor;
};

////////
//
// formating.

struct format_spec
{
	u8 is_valid;

	char fill;
	u8 minwidth;
	u8 base;

	u32 pad;

	const char *rest;
};

inline bool
is_digit(char c) { return c >= '0' && c <= '9'; }

internal char *
to_string(char *f, char *l, u64 num, u32 base = 10, ptrdiff_t minwidth = 0, char fillchar = ' ')
{
	if (f == l)
		return f;

	assert(base <= 16);

	char digits[64];
	char *p = digits;

	do
		*p++ = "0123456789ABCDEF"[num % base];
	while (num /= base);

	ptrdiff_t n = max(0LL, minwidth - (p - digits));
	f = fill_n(min(n, l - f - 1), f, fillchar);

	while (p != digits && f + 1 != l)
		*f++ = *--p;

	*f = 0;
	return f;
}

internal format_spec
parse_format_spec(const char *b)
{
	format_spec spec = {};
	spec.rest = b;

	if (*b == '0') {
		spec.fill = '0';
		++b;
	}
	else {
		spec.fill = ' ';
	}

	// todo: check for overflow string to int
	while (is_digit(*b)) {
		spec.minwidth *= 10;
		spec.minwidth += *b - '0';
		++b;
	}

	switch (*b) {
		case 'b': {
			spec.base = 2;
			++b;
		} break;

		case 'o': {
			spec.base = 8;
			++b;
		} break;

		case 'd': {
			spec.base = 10;
			++b;
		} break;

		case 'x': {
			spec.base = 16;
			++b;
		} break;

		default: {
			goto done;	// invalid format specifier
		} break;
	}

	spec.is_valid = true;
	spec.rest = b;
done:
	return spec;
}

internal char *
fmt(char *b, char *e, const char *sf, i32 x)
{
	if (b == e)
		return b;

	while (b + 1 != e && *sf) {
		if (*sf == '%') {
			++sf;
			if (*sf == '%') {
				b = copy_string(b, e, "%");
				++sf;
			}
			else {
				format_spec spec = parse_format_spec(sf);
				assert(spec.is_valid);

				u64 num = (u64)x;
				if (spec.base == 10 && x < 0) {
					b = copy_string(b, e, "-");
					if (spec.minwidth > 0) {
						--spec.minwidth;
					}
					num = (u64)-x;
				}
				b = to_string(b, e, num, spec.base, spec.minwidth, spec.fill);
				sf = spec.rest;
			}
		}
		else {
			*b++ = *sf++;
		}
	}

	*b = 0;
	return b;
}

////////

// todo: https://stackoverflow.com/questions/4572556/concise-way-to-implement-round-in-c/4572877#4572877
inline f32
round(f32 x)
{
    return (f32)(i32)(x + 0.5f);
}

internal inline f32
line_height(struct font *font)
{
	return (f32)(font->height + font->external_leading);
}

internal inline void
opengl_attach_shader(u32 program, const char *src, u32 type)
{
	u32 shader = glCreateShader(type);
	glShaderSource(shader, 1, &src, 0);
	glCompileShader(shader);

	i32 status;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (!status) {
		assert(status);
	}

	glAttachShader(program, shader);
	glDeleteShader(shader);
}

internal u32
opengl_program(const char *vs_src, const char *fs_src)
{
	u32 program = glCreateProgram();

	opengl_attach_shader(program, vs_src, GL_VERTEX_SHADER);
	opengl_attach_shader(program, fs_src, GL_FRAGMENT_SHADER);

	glLinkProgram(program);

	i32 status;
	glGetProgramiv(program, GL_LINK_STATUS, &status);
	if (!status) {
		assert(status);
	}

	return program;
}

internal i32
opengl_uniform_location(u32 program, const char *name)
{
	i32 result = glGetUniformLocation(program, name);
	assert(result != -1);
	return result;
}

////////

internal struct glyph *
render_glyph(struct app_state *state, struct font *font, u32 codepoint)
{
	for (auto& g : font->glyphs)
		if (g.codepoint == codepoint)
			return &g;

	glyph *g = allocate_n(font->glyphs, 1);

	g->codepoint = codepoint;
	g->xadv = sys_render_glyph(font, codepoint);

	i32 xmin = state->atlas_width;
	i32 ymin = state->atlas_height;
	i32 xmax = 0;
	i32 ymax = 0;

	u32 *p = font->bits;
	for (i32 y = 0; y < font->bitmap_height; ++y) {
		for (i32 x = 0; x < font->bitmap_width; ++x) {
			if (*p) {
				xmin = min(xmin, x);
				xmax = max(x, xmax);
				ymin = min(ymin, y);
				ymax = max(y, ymax);
			}
			++p;
		}
	}

	if (xmin <= xmax) {
		i32 w = xmax - xmin + 1;
		i32 h = ymax - ymin + 1;

		if (state->atlas_x + w > state->atlas_width) {
			state->atlas_x = 0;
			state->atlas_y += state->atlas_ymax;
			state->atlas_ymax = 0;
		}

		state->atlas_ymax = max(h, state->atlas_ymax);

		g->dx = xmin - font->default_x;
		g->dy = ymin - font->default_y;
		g->x0 = state->atlas_x;
		g->y0 = state->atlas_y;
		g->x1 = g->x0 + w;
		g->y1 = g->y0 + h;

		auto convert_pixel = [](u32 x) {
			u32 c = x & 0xFF;
			return (c << 24) | (c << 16) | (c << 8) | c;
		};

		u32 *src = font->bits + ymin * font->bitmap_width + xmin;
		u32 *dst = state->atlas_bits + state->atlas_y * state->atlas_width + state->atlas_x;

		for (i32 y = 0; y < h; ++y) {
			transform_n(w, dst, src, convert_pixel);
			src += font->bitmap_width;
			dst += state->atlas_width;
		}

		state->atlas_x += w;
		state->atlas_is_dirty = true;
	}
	else {
		g->dx = 0;
		g->dy = 0;
		g->x0 = 0;
		g->y0 = 0;
		g->x1 = 0;
		g->y1 = 0;
	}

	return g;
}

internal inline void
upload_vertices(app_state *state)
{
	glBindBuffer(GL_ARRAY_BUFFER, state->vbo);
	glBufferData(GL_ARRAY_BUFFER, (ptrdiff_t)(sizeof(vertex) * state->vertices.count), state->vertices.data, GL_STREAM_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

internal void
mesh_rect2d(vertex_buffer& vertices, f32 x0, f32 y0, f32 x1, f32 y1, f32 z, f32 u0, f32 v0, f32 u1, f32 v1, vec4 color)
{
	vertex *v = allocate_n(vertices, 6);

	v[0] = { { x0, y0, z }, { u0, v0 }, color };
	v[1] = { { x1, y0, z }, { u1, v0 }, color };
	v[2] = { { x0, y1, z }, { u0, v1 }, color };

	v[3] = { { x1, y0, z }, { u1, v0 }, color };
	v[4] = { { x1, y1, z }, { u1, v1 }, color };
	v[5] = { { x0, y1, z }, { u0, v1 }, color };
}

internal vec2
draw_text(app_state *state, struct font *font, const char *s, vec2 cursor, f32 z, vec4 color)
{
	f32 w = (f32)state->atlas_width;
	f32 h = (f32)state->atlas_height;

	clear(state->vertices);

	f32 x = round(cursor.x);
	f32 y = round(cursor.y);

	while (*s) {
		u32 codepoint = (u32)*s;

		if (codepoint == '\n') {
			y -= line_height(font);
			x = round(cursor.x);
		}
		else {
			struct glyph *glyph = render_glyph(state, font, codepoint);
			if (glyph) {
				f32 x0 = (f32)(x + glyph->dx);
				f32 y0 = (f32)(y + glyph->dy);
				f32 x1 = x0 + (glyph->x1 - glyph->x0);
				f32 y1 = y0 + (glyph->y1 - glyph->y0);

				f32 u0 = glyph->x0 / w;
				f32 v0 = glyph->y0 / h;
				f32 u1 = glyph->x1 / w;
				f32 v1 = glyph->y1 / h;

				mesh_rect2d(state->vertices, x0, y0, x1, y1, z, u0, v0, u1, v1, color);

				x += glyph->xadv;
			}
		}

		++s;
	}

	if (is_empty(state->vertices))
		return cursor;

	if (state->atlas_is_dirty) {
		glBindTexture(GL_TEXTURE_2D, state->atlas);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, state->atlas_width, state->atlas_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, state->atlas_bits);
		glBindTexture(GL_TEXTURE_2D, 0);
		state->atlas_is_dirty = false;
	}

	upload_vertices(state);

	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glBindTexture(GL_TEXTURE_2D, state->atlas);
	glUseProgram(state->texture_program);
	glDrawArrays(GL_TRIANGLES, 0, state->vertices.count);
	glUseProgram(0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glDisable(GL_BLEND);

	cursor.x = x;
	cursor.y = y;
	return cursor;
}

internal inline void
draw_rect2d(app_state *state, rect2d position, f32 z, vec4 color)
{
	clear(state->vertices);
	mesh_rect2d(state->vertices, position.x0, position.y0, position.x1, position.y1, z, 0.f, 0.f, 0.f, 0.f, color);

	upload_vertices(state);

	glUseProgram(state->basic_program);
	glDrawArrays(GL_TRIANGLES, 0, state->vertices.count);
	glUseProgram(0);
}

internal void
debug_text(app_state *state, const char *s)
{
	constexpr f32 z = 0.f;
	constexpr vec4 white_color = { 1.f, 1.f, 1.f, 1.f };

	state->debug_cursor = draw_text(state, state->console_font, s, state->debug_cursor, z, white_color);
}

API_EXPORT void *
reload(void *userdata)
{
	if (userdata)
		return userdata;

	app_state *state = allocate<app_state>(1);

	glGenVertexArrays(1, &state->vao);
	glBindVertexArray(state->vao);

	glGenBuffers(1, &state->vbo);
	glBindBuffer(GL_ARRAY_BUFFER, state->vbo);

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(0, 3, GL_FLOAT, false, sizeof(vertex), (void *)offsetof(vertex, position));
	glVertexAttribPointer(1, 2, GL_FLOAT, false, sizeof(vertex), (void *)offsetof(vertex, texcoord));
	glVertexAttribPointer(2, 4, GL_FLOAT, false, sizeof(vertex), (void *)offsetof(vertex, color));

	glBindBuffer(GL_ARRAY_BUFFER, 0);

	const char *basic_vs_src = R"(#version 330

		layout(location = 0) in vec3 vs_position;
		layout(location = 1) in vec2 vs_texcoord;
		layout(location = 2) in vec4 vs_color;

		out vec4 fs_color;

		uniform mat4 proj;

		void main(void)
		{
			fs_color = vs_color;
			gl_Position = proj * vec4(vs_position, 1);
		}
	)";

	const char *basic_fs_src = R"(#version 330

		in vec4 fs_color;

		out vec4 frag_color;

		void main(void)
		{
			frag_color = fs_color;
		}
	)";
	state->basic_program = opengl_program(basic_vs_src, basic_fs_src);
	state->basic_uproj = opengl_uniform_location(state->basic_program, "proj");

	const char *texture_vs_src = R"(#version 330

		layout(location = 0) in vec3 vs_position;
		layout(location = 1) in vec2 vs_texcoord;
		layout(location = 2) in vec4 vs_color;

		out vec2 fs_texcoord;
		out vec4 fs_color;

		uniform mat4 proj;

		void main(void)
		{
			fs_texcoord = vs_texcoord;
			fs_color = vs_color;
			gl_Position = proj * vec4(vs_position, 1);
		}
	)";

	const char *texture_fs_src = R"(#version 330

		in vec2 fs_texcoord;
		in vec4 fs_color;

		out vec4 frag_color;

		uniform sampler2D texture_map;

		void main(void)
		{
			frag_color = texture(texture_map, fs_texcoord) * fs_color;
		}
	)";
	state->texture_program = opengl_program(texture_vs_src, texture_fs_src);
	state->texture_uproj = opengl_uniform_location(state->texture_program, "proj");
	state->texture_umap = opengl_uniform_location(state->texture_program, "texture_map");

	state->atlas_width = 512;
	state->atlas_height = 512;
	state->atlas_bits = allocate<u32>((size_t)(state->atlas_width * state->atlas_height));

	glGenTextures(1, &state->atlas);
	glBindTexture(GL_TEXTURE_2D, state->atlas);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, state->atlas_width, state->atlas_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, state->atlas_bits);
	glBindTexture(GL_TEXTURE_2D, 0);

	glEnable(GL_FRAMEBUFFER_SRGB);

	reserve(state->vertices, 1024 * 64);

	// load assets
	state->console_font = sys_create_font(L"Courier New", 10);
	state->ui_font = sys_create_font(L"Verdana", 8);

	for (u32 codepoint = ' '; codepoint < 127; ++codepoint) {
		render_glyph(state, state->console_font, codepoint);
		render_glyph(state, state->ui_font, codepoint);
	}

	return state;
}

API_EXPORT void
mouse(void *userdata, i32 x, i32 y, i32 dz, u32 buttons)
{
	unused(dz);

	app_state *state = (app_state *)userdata;

	state->mouse_x = x;
	state->mouse_y = y;
	state->mouse_buttons = buttons;
}

API_EXPORT void
keyboard(void *userdata, u32 codepoint)
{
	unused(userdata);
	unused(codepoint);
}

API_EXPORT void
render(void *userdata, i32 window_width, i32 window_height)
{
	app_state *state = (app_state *)userdata;

	glViewport(0, 0, window_width, window_height);

	f32 sx = 2.f / window_width;
	f32 sy = 2.f / window_height;
	f32 proj[16] = {
		  sx,  0.f, 0.f, 0.f,
		 0.f,   sy, 0.f, 0.f,
		 0.f,  0.f, 1.f, 0.f,
		-1.f, -1.f, 0.f, 1.f,
	};
	glUseProgram(state->basic_program);
	glUniformMatrix4fv(state->basic_uproj, 1, false, proj);
	glUseProgram(state->texture_program);
	glUniformMatrix4fv(state->texture_uproj, 1, false, proj);
	glUseProgram(0);

	glClearColor(0.02f, 0.02f, 0.02f, 1.f);
	glClear(GL_COLOR_BUFFER_BIT);

	////////
	//
	// some test rendering.

	f32 z = 0.f;

	vec4 white_color = { 1.f, 1.f, 1.f, 1.f };
	vec4 red_color = { 1.f, 0.f, 0.f, 1.f };

	vec2 cursor = { 100.f, 80.f };

	const char *s = "The quick brown fox jumps over the lazy dog.\n";
	cursor = draw_text(state, state->console_font, s, cursor, z, white_color);
	cursor = draw_text(state, state->ui_font, s, cursor, z, white_color);

	struct rect2d bounds = { 0.f, (f32)window_height - 32.f, (f32)window_width, (f32)window_height };
	draw_rect2d(state, bounds, z, red_color);

	////////
	//
	// display user input state.

	state->debug_cursor.x = (f32)window_width - 250.f;
	state->debug_cursor.y = (f32)window_height - line_height(state->console_font);

	char buf[64];
	char *end = buf + sizeof(buf);

	fmt(buf, end, "mouse x: %d\n", state->mouse_x);
	debug_text(state, buf);
	fmt(buf, end, "mouse y: %d\n", state->mouse_y);
	debug_text(state, buf);
	fmt(buf, end, "buttons: 0x%08x\n", (i32)state->mouse_buttons);
	debug_text(state, buf);
}

extern "C" int _fltused = 0;
