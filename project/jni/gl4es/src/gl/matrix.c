#include "matrix.h"
#include "gl.h"
#include "debug.h"

//#define DEBUG
#ifdef DEBUG
#define DBG(a) a
#else
#define DBG(a)
#endif

void alloc_matrix(matrixstack_t **matrixstack, int depth) {
	*matrixstack = (matrixstack_t*)malloc(sizeof(matrixstack_t));
	(*matrixstack)->top = 0;
	(*matrixstack)->identity = 0;
	(*matrixstack)->stack = (GLfloat*)malloc(sizeof(GLfloat)*depth*16);
}

#define TOP(A) (glstate->A->stack+(glstate->A->top*16))

static GLfloat* update_current_mat() {
	switch(glstate->matrix_mode) {
		case GL_MODELVIEW:
			return TOP(modelview_matrix);
		case GL_PROJECTION:
			return TOP(projection_matrix);
		case GL_TEXTURE:
			return TOP(texture_matrix[glstate->texture.active]);
	}
	return NULL;
}

static int update_current_identity(int I) {
	switch(glstate->matrix_mode) {
		case GL_MODELVIEW:
			return glstate->modelview_matrix->identity = (I)?1:is_identity(TOP(modelview_matrix));
		case GL_PROJECTION:
			return glstate->projection_matrix->identity = (I)?1:is_identity(TOP(projection_matrix));
		case GL_TEXTURE:
			return glstate->texture_matrix[glstate->texture.active]->identity = (I)?1:is_identity(TOP(texture_matrix[glstate->texture.active]));
	}
	return 0;
}

static int send_to_hardware() {
	switch(glstate->matrix_mode) {
		case GL_PROJECTION:
			return 1;
		case GL_MODELVIEW:
			return 1;
		case GL_TEXTURE:
			return 0;
	}
	return 0;
}

void init_matrix(glstate_t* glstate) {
    alloc_matrix(&glstate->projection_matrix, MAX_STACK_PROJECTION);
    set_identity(TOP(projection_matrix));
	glstate->projection_matrix->identity = 1;
    alloc_matrix(&glstate->modelview_matrix, MAX_STACK_MODELVIEW);
    set_identity(TOP(modelview_matrix));
	glstate->modelview_matrix->identity = 1;
    glstate->texture_matrix = (matrixstack_t**)malloc(sizeof(matrixstack_t*)*MAX_TEX);
    for (int i=0; i<MAX_TEX; i++) {
        alloc_matrix(&glstate->texture_matrix[i], MAX_STACK_TEXTURE);
        set_identity(TOP(texture_matrix[i]));
		glstate->texture_matrix[i]->identity = 1;
    }
}

void gl4es_glMatrixMode(GLenum mode) {
DBG(printf("glMatrixMode(%s), list=%p\n", PrintEnum(mode), glstate->list.active);)
	PUSH_IF_COMPILING(glMatrixMode);
	LOAD_GLES(glMatrixMode);

	if(!(mode==GL_MODELVIEW || mode==GL_PROJECTION || mode==GL_TEXTURE)) {
		errorShim(GL_INVALID_ENUM);
		return;
	}
    if(glstate->matrix_mode != mode) {
        glstate->matrix_mode = mode;
        gles_glMatrixMode(mode);
    }
}

void gl4es_glPushMatrix() {
DBG(printf("glPushMatrix(), list=%p\n", glstate->list.active);)
	PUSH_IF_COMPILING(glPushMatrix);
	// get matrix mode
	GLint matrix_mode = glstate->matrix_mode;
	noerrorShim();
	// go...
	switch(matrix_mode) {
		#define P(A, B) if(glstate->A->top<MAX_STACK_##B) { \
			memcpy(TOP(A)+16, TOP(A), 16*sizeof(GLfloat)); \
			glstate->A->top++; \
		} else errorShim(GL_STACK_OVERFLOW)
		case GL_PROJECTION:
			P(projection_matrix, PROJECTION);
			break;
		case GL_MODELVIEW:
			P(modelview_matrix, MODELVIEW);
			break;
		case GL_TEXTURE:
			P(texture_matrix[glstate->texture.active], TEXTURE);
			break;
		#undef P
		default:
			//Warning?
			errorShim(GL_INVALID_OPERATION);
			//LOGE("LIBGL: PushMatrix with Unrecognise matrix mode (0x%04X)\n", matrix_mode);
			//gles_glPushMatrix();
	}
}

void gl4es_glPopMatrix() {
DBG(printf("glPopMatrix(), list=%p\n", glstate->list.active);)
	PUSH_IF_COMPILING(glPopMatrix);
	LOAD_GLES(glLoadMatrixf);
	// get matrix mode
	GLint matrix_mode = glstate->matrix_mode;
	// go...
	noerrorShim();
	switch(matrix_mode) {
		#define P(A) if(glstate->A->top) { \
			--glstate->A->top; \
			glstate->A->identity = is_identity(update_current_mat()); \
			if (send_to_hardware()) gles_glLoadMatrixf(update_current_mat()); \
		} else errorShim(GL_STACK_UNDERFLOW)
		case GL_PROJECTION:
			P(projection_matrix);
			break;
		case GL_MODELVIEW:
			P(modelview_matrix);
			break;
		case GL_TEXTURE:
			P(texture_matrix[glstate->texture.active]);
			break;
		#undef P
			
		default:
			//Warning?
			errorShim(GL_INVALID_OPERATION);
			//LOGE("LIBGL: PopMatrix with Unrecognise matrix mode (0x%04X)\n", matrix_mode);
			//gles_glPopMatrix();
	}
}

void gl4es_glLoadMatrixf(const GLfloat * m) {
DBG(printf("glLoadMatrix(%f, %f, %f, %f, %f, %f, %f...), list=%p\n", m[0], m[1], m[2], m[3], m[4], m[5], m[6], glstate->list.active);)
    LOAD_GLES(glLoadMatrixf);
	LOAD_GLES(glLoadIdentity);

    if ((glstate->list.compiling || glstate->gl_batch) && glstate->list.active) {
        NewStage(glstate->list.active, STAGE_MATRIX);
        glstate->list.active->matrix_op = 1;
        memcpy(glstate->list.active->matrix_val, m, 16*sizeof(GLfloat));
        return;
    }
	memcpy(update_current_mat(), m, 16*sizeof(GLfloat));
	const int id = update_current_identity(0);
    if(send_to_hardware()) 
		if(id) gles_glLoadIdentity();	// in case the driver as some special optimisations
		else gles_glLoadMatrixf(m);
}

void gl4es_glMultMatrixf(const GLfloat * m) {
DBG(printf("glMultMatrix(%f, %f, %f, %f, %f, %f, %f...), list=%p\n", m[0], m[1], m[2], m[3], m[4], m[5], m[6], glstate->list.active);)
    LOAD_GLES(glLoadMatrixf);
	LOAD_GLES(glLoadIdentity);
    if ((glstate->list.compiling || glstate->gl_batch) && glstate->list.active) {
		if(glstate->list.active->stage == STAGE_MATRIX) {
			// multiply the matrix mith the current one....
			matrix_mul(glstate->list.active->matrix_val, m, glstate->list.active->matrix_val);
			return;
		}
        NewStage(glstate->list.active, STAGE_MATRIX);
        glstate->list.active->matrix_op = 2;
        memcpy(glstate->list.active->matrix_val, m, 16*sizeof(GLfloat));
        return;
    }
	GLfloat *current_mat = update_current_mat();
	matrix_mul(current_mat, m, current_mat);
	const int id = update_current_identity(0);
    if(send_to_hardware())
		if(id) gles_glLoadIdentity();	// in case the driver as some special optimisations
		else gles_glLoadMatrixf(current_mat);
}

void gl4es_glLoadIdentity() {
DBG(printf("glLoadIdentity(), list=%p\n", glstate->list.active);)
	LOAD_GLES(glLoadIdentity);
    if ((glstate->list.compiling || glstate->gl_batch) && glstate->list.active) {
        NewStage(glstate->list.active, STAGE_MATRIX);
        glstate->list.active->matrix_op = 1;
        set_identity(glstate->list.active->matrix_val);
        return;
    }
	
	set_identity(update_current_mat());
	update_current_identity(1);
	if(send_to_hardware()) gles_glLoadIdentity();
}

void gl4es_glTranslatef(GLfloat x, GLfloat y, GLfloat z) {
DBG(printf("glTranslatef(%f, %f, %f), list=%p\n", x, y, z, glstate->list.active);)
	// create a translation matrix than multiply it...
	GLfloat tmp[16];
	set_identity(tmp);
	tmp[12+0] = x;
	tmp[12+1] = y;
	tmp[12+2] = z;
	gl4es_glMultMatrixf(tmp);
}

void gl4es_glScalef(GLfloat x, GLfloat y, GLfloat z) {
DBG(printf("glScalef(%f, %f, %f), list=%p\n", x, y, z, glstate->list.active);)
	// create a scale matrix than multiply it...
	GLfloat tmp[16];
	memset(tmp, 0, 16*sizeof(GLfloat));
	tmp[0+0] = x;
	tmp[1+4] = y;
	tmp[2+8] = z;
	tmp[3+12] = 1.0f;
	gl4es_glMultMatrixf(tmp);
}

void gl4es_glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z) {
DBG(printf("glRotatef(%f, %f, %f, %f), list=%p\n", angle, x, y, z, glstate->list.active);)
	// create a rotation matrix than multiply it...
	GLfloat tmp[16];
	memset(tmp, 0, 16*sizeof(GLfloat));
	if((x==0 && y==0 && z==0) || angle==0)
		return;	// nothing to do
	// normalize x y z
	GLfloat l = 1.0f/sqrtf(x*x+y*y+z*z);
	x=x*l; y=y*l; z=z*l;
	// calculate sin/cos
	angle*=3.1415926535f/180.f;
	const GLfloat s=sinf(angle); 
	const GLfloat c=cosf(angle);
	const GLfloat c1 = 1-c;
	//build the matrix
	tmp[0+0] = x*x*c1+c;   tmp[0+4] = x*y*c1-z*s; tmp[0+8] = x*z*c1+y*s;
	tmp[1+0] = y*x*c1+z*s; tmp[1+4] = y*y*c1+c;   tmp[1+8] = y*z*c1-x*s;
	tmp[2+0] = x*z*c1-y*s; tmp[2+4] = y*z*c1+x*s; tmp[2+8] = z*z*c1+c;

	tmp[3+12] = 1.0f;
	// done...
	gl4es_glMultMatrixf(tmp);
}

void gl4es_glOrthof(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat nearVal, GLfloat farVal) {
DBG(printf("glOrthof(%f, %f, %f, %f, %f, %f), list=%p\n", left, right, top, bottom, nearVal, farVal, glstate->list.active);)
	GLfloat tmp[16];
	memset(tmp, 0, 16*sizeof(GLfloat));

	tmp[0+0] = 2.0f/(right-left);     tmp[0+12] = -(right+left)/(right-left);
	tmp[1+4] = 2.0f/(top-bottom);     tmp[1+12] = -(top+bottom)/(top-bottom);
	tmp[2+8] =-2.0f/(farVal-nearVal); tmp[2+12] = -(farVal+nearVal)/(farVal-nearVal);
	                                  tmp[3+12] = 1.0f;

	gl4es_glMultMatrixf(tmp);
}

void gl4es_glFrustumf(GLfloat left,	GLfloat right, GLfloat bottom, GLfloat top,	GLfloat nearVal, GLfloat farVal) {
DBG(printf("glFrustumf(%f, %f, %f, %f, %f, %f) list=%p\n", left, right, top, bottom, nearVal, farVal, glstate->list.active);)
	GLfloat tmp[16];
	memset(tmp, 0, 16*sizeof(GLfloat));

	tmp[0+0] = 2.0f*nearVal/(right-left);	tmp[0+8] = (right+left)/(right-left);
	tmp[1+4] = 2.0f*nearVal/(top-bottom);   tmp[1+8] = (top+bottom)/(top-bottom);
											tmp[2+8] =-(farVal+nearVal)/(farVal-nearVal); tmp[2+12] =-2.0f*farVal*nearVal/(farVal-nearVal);
	                                  		tmp[3+8] = -1.0f;

	gl4es_glMultMatrixf(tmp);
}

void glMatrixMode(GLenum mode) AliasExport("gl4es_glMatrixMode");
void glPushMatrix() AliasExport("gl4es_glPushMatrix");
void glPopMatrix() AliasExport("gl4es_glPopMatrix");
void glLoadMatrixf(const GLfloat * m) AliasExport("gl4es_glLoadMatrixf");
void glMultMatrixf(const GLfloat * m) AliasExport("gl4es_glMultMatrixf");
void glLoadIdentity() AliasExport("gl4es_glLoadIdentity");
void glTranslatef(GLfloat x, GLfloat y, GLfloat z) AliasExport("gl4es_glTranslatef");
void glScalef(GLfloat x, GLfloat y, GLfloat z) AliasExport("gl4es_glScalef");
void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z) AliasExport("gl4es_glRotatef");
void glOrthof(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat nearVal, GLfloat farVal) AliasExport("gl4es_glOrthof");
void glFrustumf(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat nearVal, GLfloat farVal) AliasExport("gl4es_glFrustumf");
