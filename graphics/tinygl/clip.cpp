
#include "graphics/tinygl/zgl.h"

namespace TinyGL {

// fill triangle profile
// #define TINYGL_PROFILE

#define CLIP_XMIN   (1 << 0)
#define CLIP_XMAX   (1 << 1)
#define CLIP_YMIN   (1 << 2)
#define CLIP_YMAX   (1 << 3)
#define CLIP_ZMIN   (1 << 4)
#define CLIP_ZMAX   (1 << 5)

void gl_transform_to_viewport(GLContext *c, GLVertex *v) {
	float winv;

	// coordinates
	winv = (float)(1.0 / v->pc.getW());
	v->zp.x = (int)(v->pc.getX() * winv * c->viewport.scale.getX() + c->viewport.trans.getX());
	v->zp.y = (int)(v->pc.getY() * winv * c->viewport.scale.getY() + c->viewport.trans.getY());
	v->zp.z = (int)(v->pc.getZ() * winv * c->viewport.scale.getZ() + c->viewport.trans.getZ());
	// color
	if (c->lighting_enabled) {
		v->zp.r = (int)(v->color.getX() * (ZB_POINT_RED_MAX - ZB_POINT_RED_MIN)
					+ ZB_POINT_RED_MIN);
		v->zp.g = (int)(v->color.getY() * (ZB_POINT_GREEN_MAX - ZB_POINT_GREEN_MIN)
					+ ZB_POINT_GREEN_MIN);
		v->zp.b = (int)(v->color.getZ() * (ZB_POINT_BLUE_MAX - ZB_POINT_BLUE_MIN)
					+ ZB_POINT_BLUE_MIN);
	} else {
		// no need to convert to integer if no lighting : take current color
		v->zp.r = c->longcurrent_color[0];
		v->zp.g = c->longcurrent_color[1];
		v->zp.b = c->longcurrent_color[2];
	}

	// texture
	if (c->texture_2d_enabled) {
		v->zp.s = (int)(v->tex_coord.getX() * (ZB_POINT_S_MAX - ZB_POINT_S_MIN) + ZB_POINT_S_MIN);
		v->zp.t = (int)(v->tex_coord.getY() * (ZB_POINT_S_MAX - ZB_POINT_S_MIN) + ZB_POINT_S_MIN);
	}
}

static void gl_add_select1(GLContext *c, int z1, int z2, int z3) {
	int min, max;

	min = max = z1;
	if (z2 < min)
		min = z2;
	if (z3 < min)
		min = z3;
	if (z2 > max)
		max = z2;
	if (z3 > max)
		max = z3;

	gl_add_select(c, 0xffffffff - min, 0xffffffff - max);
}

// point

void gl_draw_point(GLContext *c, GLVertex *p0) {
	if (p0->clip_code == 0) {
		if (c->render_mode == TGL_SELECT) {
			gl_add_select(c, p0->zp.z, p0->zp.z);
		} else {
			ZB_plot(c->zb, &p0->zp);
		}
	}
}

// line

static inline void interpolate(GLVertex *q, GLVertex *p0, GLVertex *p1, float t) {

	q->pc = p0->pc + (p1->pc - p0->pc) * t;
	q->color = p0->color + (p1->color - p0->color) * t;
}

// Line Clipping

// Line Clipping algorithm from 'Computer Graphics', Principles and
// Practice
static inline int ClipLine1(float denom, float num, float *tmin, float *tmax) {
	float t;

	if (denom > 0) {
		t = num / denom;
		if (t > *tmax)
			return 0;
		if (t > *tmin)
			*tmin = t;
	} else if (denom < 0) {
		t = num / denom;
		if (t < *tmin)
			return 0;
		if (t < *tmax)
			*tmax = t;
	} else if (num > 0)
		return 0;

	return 1;
}

void gl_draw_line(GLContext *c, GLVertex *p1, GLVertex *p2) {
	float dx, dy, dz, dw, x1, y1, z1, w1;
	float tmin, tmax;
	GLVertex q1, q2;
	int cc1, cc2;

	cc1 = p1->clip_code;
	cc2 = p2->clip_code;

	if ((cc1 | cc2) == 0) {
		if (c->render_mode == TGL_SELECT) {
			gl_add_select1(c, p1->zp.z, p2->zp.z, p2->zp.z);
		} else {
			if (c->depth_test)
				ZB_line_z(c->zb, &p1->zp, &p2->zp);
			else
				ZB_line(c->zb, &p1->zp, &p2->zp);
		}
	} else if ((cc1 & cc2) != 0) {
		return;
	} else {
		dx = p2->pc.getX() - p1->pc.getX();
		dy = p2->pc.getY() - p1->pc.getY();
		dz = p2->pc.getZ() - p1->pc.getZ();
		dw = p2->pc.getW() - p1->pc.getW();
		x1 = p1->pc.getX();
		y1 = p1->pc.getY();
		z1 = p1->pc.getZ();
		w1 = p1->pc.getW();

		tmin = 0;
		tmax = 1;
		if (ClipLine1(dx + dw, -x1 - w1, &tmin, &tmax) &&
				ClipLine1(-dx + dw, x1 - w1, &tmin, &tmax) &&
				ClipLine1(dy + dw, -y1 - w1, &tmin, &tmax) &&
				ClipLine1(-dy + dw, y1 - w1, &tmin, &tmax) &&
				ClipLine1(dz + dw, -z1 - w1, &tmin, &tmax) &&
				ClipLine1(-dz + dw, z1 - w1, &tmin, &tmax)) {
			interpolate(&q1, p1, p2, tmin);
			interpolate(&q2, p1, p2, tmax);
			gl_transform_to_viewport(c, &q1);
			gl_transform_to_viewport(c, &q2);

			if (c->depth_test)
				ZB_line_z(c->zb, &q1.zp, &q2.zp);
			else
				ZB_line(c->zb, &q1.zp, &q2.zp);
		}
	}
}

// triangle

// Clipping

// We clip the segment [a,b] against the 6 planes of the normal volume.
// We compute the point 'c' of intersection and the value of the parameter 't'
// of the intersection if x=a+t(b-a).

#define clip_func(name, sign, dir, dir1, dir2) \
static float name(Vector4 *c, Vector4 *a, Vector4 *b) { \
	float t, dX, dY, dZ, dW, den;\
	dX = (b->getX() - a->getX()); \
	dY = (b->getY() - a->getY()); \
	dZ = (b->getZ() - a->getZ()); \
	dW = (b->getW() - a->getW()); \
	den = -(sign d ## dir) + dW; \
	if (den == 0) \
		t = 0; \
	else \
		t = (sign a->get ## dir () - a->getW()) / den; \
	c->set ## dir1 (a->get ## dir1 () + t * d ## dir1); \
	c->set ## dir2 (a->get ## dir2 () + t * d ## dir2); \
	c->setW(a->getW() + t * dW); \
	c->set ## dir (sign c->getW()); \
	return t; \
}

clip_func(clip_xmin, -, X, Y, Z)
clip_func(clip_xmax, +, X, Y, Z)
clip_func(clip_ymin, -, Y, X, Z)
clip_func(clip_ymax, +, Y, X, Z)
clip_func(clip_zmin, -, Z, X, Y)
clip_func(clip_zmax, +, Z, X, Y)

float(*clip_proc[6])(Vector4 *, Vector4 *, Vector4 *) =  {
	clip_xmin, clip_xmax,
	clip_ymin, clip_ymax,
	clip_zmin, clip_zmax
};

static inline void updateTmp(GLContext *c, GLVertex *q,
							 GLVertex *p0, GLVertex *p1, float t) {
	if (c->current_shade_model == TGL_SMOOTH) {
		float a = q->color.getW();
		q->color = p0->color + (p1->color - p0->color) * t;
		q->color.setW(a);
	} else {
		q->color.setX(p0->color.getX());
		q->color.setY(p0->color.getY());
		q->color.setZ(p0->color.getZ());
		//q->color = p0->color;
	}

	if (c->texture_2d_enabled) {
		//NOTE: This could be implemented with operator overloading, but i'm not 100% sure that we can completely disregard Z and W components so I'm leaving it like this for now.
		q->tex_coord.setX(p0->tex_coord.getX() + (p1->tex_coord.getX() - p0->tex_coord.getX()) * t);
		q->tex_coord.setY(p0->tex_coord.getY() + (p1->tex_coord.getY() - p0->tex_coord.getY()) * t);
	}

	q->clip_code = gl_clipcode(q->pc.getX(), q->pc.getY(), q->pc.getZ(), q->pc.getW());
	if (q->clip_code == 0)
		gl_transform_to_viewport(c, q);
}

static void gl_draw_triangle_clip(GLContext *c, GLVertex *p0,
								  GLVertex *p1, GLVertex *p2, int clip_bit);

void gl_draw_triangle(GLContext *c, GLVertex *p0, GLVertex *p1, GLVertex *p2) {
	int co, c_and, cc[3], front;
	float norm;

	cc[0] = p0->clip_code;
	cc[1] = p1->clip_code;
	cc[2] = p2->clip_code;

	co = cc[0] | cc[1] | cc[2];

	// we handle the non clipped case here to go faster
	if (co == 0) {
		norm = (float)(p1->zp.x - p0->zp.x) * (float)(p2->zp.y - p0->zp.y) -
			   (float)(p2->zp.x - p0->zp.x) * (float)(p1->zp.y - p0->zp.y);
		if (norm == 0)
			return;

		front = norm < 0.0;
		front = front ^ c->current_front_face;

		// back face culling
		if (c->cull_face_enabled) {
			// most used case first */
			if (c->current_cull_face == TGL_BACK) {
				if (front == 0)
					return;
				c->draw_triangle_front(c, p0, p1, p2);
			} else if (c->current_cull_face == TGL_FRONT) {
				if (front != 0)
					return;
				c->draw_triangle_back(c, p0, p1, p2);
			} else {
				return;
			}
		} else {
			// no culling
			if (front) {
				c->draw_triangle_front(c, p0, p1, p2);
			} else {
				c->draw_triangle_back(c, p0, p1, p2);
			}
		}
	} else {
		c_and = cc[0] & cc[1] & cc[2];
		if (c_and == 0) {
			gl_draw_triangle_clip(c, p0, p1, p2, 0);
		}
	}
}

static void gl_draw_triangle_clip(GLContext *c, GLVertex *p0,
								  GLVertex *p1, GLVertex *p2, int clip_bit) {
	int co, c_and, co1, cc[3], edge_flag_tmp, clip_mask;
	GLVertex tmp1, tmp2, *q[3];
	float tt;

	cc[0] = p0->clip_code;
	cc[1] = p1->clip_code;
	cc[2] = p2->clip_code;

	co = cc[0] | cc[1] | cc[2];
	if (co == 0) {
		gl_draw_triangle(c, p0, p1, p2);
	} else {
		c_and = cc[0] & cc[1] & cc[2];
		// the triangle is completely outside
		if (c_and != 0)
			return;

		// find the next direction to clip
		while (clip_bit < 6 && (co & (1 << clip_bit)) == 0) {
			clip_bit++;
		}

		// this test can be true only in case of rounding errors
		if (clip_bit == 6) {
#if 0
			printf("Error:\n");
			printf("%f %f %f %f\n", p0->pc.X, p0->pc.Y, p0->pc.Z, p0->pc.W);
			printf("%f %f %f %f\n", p1->pc.X, p1->pc.Y, p1->pc.Z, p1->pc.W);
			printf("%f %f %f %f\n", p2->pc.X, p2->pc.Y, p2->pc.Z, p2->pc.W);
#endif
			return;
		}

		clip_mask = 1 << clip_bit;
		co1 = (cc[0] ^ cc[1] ^ cc[2]) & clip_mask;

		if (co1)  {
			// one point outside
			if (cc[0] & clip_mask) {
				q[0] = p0; q[1] = p1; q[2] = p2;
			} else if (cc[1] & clip_mask) {
				q[0] = p1; q[1] = p2; q[2] = p0;
			} else {
				q[0] = p2; q[1] = p0; q[2] = p1;
			}

			tt = clip_proc[clip_bit](&tmp1.pc, &q[0]->pc, &q[1]->pc);
			updateTmp(c, &tmp1, q[0], q[1], tt);

			tt = clip_proc[clip_bit](&tmp2.pc, &q[0]->pc, &q[2]->pc);
			updateTmp(c, &tmp2, q[0], q[2], tt);

			tmp1.edge_flag = q[0]->edge_flag;
			edge_flag_tmp = q[2]->edge_flag;
			q[2]->edge_flag = 0;
			gl_draw_triangle_clip(c, &tmp1, q[1], q[2], clip_bit + 1);

			tmp2.edge_flag = 1;
			tmp1.edge_flag = 0;
			q[2]->edge_flag = edge_flag_tmp;
			gl_draw_triangle_clip(c, &tmp2, &tmp1, q[2], clip_bit + 1);
		} else {
			// two points outside
			if ((cc[0] & clip_mask) == 0) {
				q[0] = p0; q[1] = p1; q[2] = p2;
			} else if ((cc[1] & clip_mask) == 0) {
				q[0] = p1; q[1] = p2; q[2] = p0;
			} else {
				q[0] = p2; q[1] = p0; q[2] = p1;
			}

			tt = clip_proc[clip_bit](&tmp1.pc, &q[0]->pc, &q[1]->pc);
			updateTmp(c, &tmp1, q[0], q[1], tt);

			tt = clip_proc[clip_bit](&tmp2.pc, &q[0]->pc, &q[2]->pc);
			updateTmp(c, &tmp2, q[0], q[2], tt);

			tmp1.edge_flag = 1;
			tmp2.edge_flag = q[2]->edge_flag;
			gl_draw_triangle_clip(c, q[0], &tmp1, &tmp2, clip_bit + 1);
		}
	}
}

void gl_draw_triangle_select(GLContext *c, GLVertex *p0, GLVertex *p1, GLVertex *p2) {
	gl_add_select1(c, p0->zp.z, p1->zp.z, p2->zp.z);
}

#ifdef TINYGL_PROFILE
int count_triangles, count_triangles_textured, count_pixels;
#endif

void gl_draw_triangle_fill(GLContext *c, GLVertex *p0, GLVertex *p1, GLVertex *p2) {
#ifdef TINYGL_PROFILE
	{
		int norm;
		assert(p0->zp.x >= 0 && p0->zp.x < c->zb->xsize);
		assert(p0->zp.y >= 0 && p0->zp.y < c->zb->ysize);
		assert(p1->zp.x >= 0 && p1->zp.x < c->zb->xsize);
		assert(p1->zp.y >= 0 && p1->zp.y < c->zb->ysize);
		assert(p2->zp.x >= 0 && p2->zp.x < c->zb->xsize);
		assert(p2->zp.y >= 0 && p2->zp.y < c->zb->ysize);

		norm = (p1->zp.x - p0->zp.x) * (p2->zp.y - p0->zp.y) -
				(p2->zp.x - p0->zp.x) * (p1->zp.y - p0->zp.y);
		count_pixels += abs(norm) / 2;
		count_triangles++;
	}
#endif

	if (c->color_mask == 0) {
		// FIXME: Accept more than just 0 or 1.
		ZB_fillTriangleDepthOnly(c->zb, &p0->zp, &p1->zp, &p2->zp);
	}
	if (c->shadow_mode & 1) {
		assert(c->zb->shadow_mask_buf);
		ZB_fillTriangleFlatShadowMask(c->zb, &p0->zp, &p1->zp, &p2->zp);
	} else if (c->shadow_mode & 2) {
		assert(c->zb->shadow_mask_buf);
		ZB_fillTriangleFlatShadow(c->zb, &p0->zp, &p1->zp, &p2->zp);
	} else if (c->texture_2d_enabled) {
#ifdef TINYGL_PROFILE
		count_triangles_textured++;
#endif
		ZB_setTexture(c->zb, c->current_texture->images[0].pixmap);
		ZB_fillTriangleMappingPerspective(c->zb, &p0->zp, &p1->zp, &p2->zp);
	} else if (c->current_shade_model == TGL_SMOOTH) {
		ZB_fillTriangleSmooth(c->zb, &p0->zp, &p1->zp, &p2->zp);
	} else {
		ZB_fillTriangleFlat(c->zb, &p0->zp, &p1->zp, &p2->zp);
	}
}

// Render a clipped triangle in line mode

void gl_draw_triangle_line(GLContext *c, GLVertex *p0, GLVertex *p1, GLVertex *p2) {
	if (c->depth_test) {
		if (p0->edge_flag)
			ZB_line_z(c->zb, &p0->zp, &p1->zp);
		if (p1->edge_flag)
			ZB_line_z(c->zb, &p1->zp, &p2->zp);
		if (p2->edge_flag)
			ZB_line_z(c->zb, &p2->zp, &p0->zp);
	} else {
		if (p0->edge_flag)
			ZB_line(c->zb, &p0->zp, &p1->zp);
		if (p1->edge_flag)
			ZB_line(c->zb, &p1->zp, &p2->zp);
		if (p2->edge_flag)
			ZB_line(c->zb, &p2->zp, &p0->zp);
	}
}

// Render a clipped triangle in point mode
void gl_draw_triangle_point(GLContext *c, GLVertex *p0, GLVertex *p1, GLVertex *p2) {
	if (p0->edge_flag)
		ZB_plot(c->zb, &p0->zp);
	if (p1->edge_flag)
		ZB_plot(c->zb, &p1->zp);
	if (p2->edge_flag)
		ZB_plot(c->zb, &p2->zp);
}

} // end of namespace TinyGL
