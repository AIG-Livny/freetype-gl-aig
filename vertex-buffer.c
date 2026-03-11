//  Freetype GL - A C OpenGL Freetype engine
//
//  Distributed under the OSI-approved BSD 2-Clause License.  See accompanying
//  file `LICENSE` for more details.

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "cgeom.h"
#include "platform.h"
#include "vertex-buffer.h"
#include "ftgl-utils.h"

//  Buffer status
#define CLEAN  (0)
#define DIRTY  (1)
#define FROZEN (2)


// ----------------------------------------------------------------------------
vertex_buffer_t *
vertex_buffer_new( const char *format )
{
    size_t i, index = 0, stride = 0;
    const char *start = 0, *end = 0;
    GLchar *pointer = 0;

    vertex_buffer_t *self = (vertex_buffer_t *) malloc (sizeof(vertex_buffer_t));
    if( !self )
    {
        return NULL;
    }

    self->format = strdup( format );

    for( i=0; i<MAX_VERTEX_ATTRIBUTE; ++i )
    {
        self->attributes[i] = 0;
    }

    start = format;
    do
    {
        char *desc = 0;
        vertex_attribute_t *attribute;
        GLuint attribute_size = 0;
        end = (char *) (strchr(start+1, ','));

        if (end == NULL)
        {
            desc = strdup( start );
        }
        else
        {
            desc = strndup( start, end-start );
        }
        attribute = vertex_attribute_parse( desc );
        start = end+1;
        free(desc);
        attribute->pointer = pointer;

        switch( attribute->type )
        {
        case GL_BOOL:           attribute_size = sizeof(GLboolean); break;
        case GL_BYTE:           attribute_size = sizeof(GLbyte); break;
        case GL_UNSIGNED_BYTE:  attribute_size = sizeof(GLubyte); break;
        case GL_SHORT:          attribute_size = sizeof(GLshort); break;
        case GL_UNSIGNED_SHORT: attribute_size = sizeof(GLushort); break;
        case GL_INT:            attribute_size = sizeof(GLint); break;
        case GL_UNSIGNED_INT:   attribute_size = sizeof(GLuint); break;
        case GL_FLOAT:          attribute_size = sizeof(GLfloat); break;
        default:                attribute_size = 0;
        }
        stride  += attribute->size*attribute_size;
        pointer += attribute->size*attribute_size;
        self->attributes[index] = attribute;
        index++;
    } while ( end && (index < MAX_VERTEX_ATTRIBUTE) );

    for( i=0; i<index; ++i )
    {
        self->attributes[i]->stride = stride;
    }

#ifdef FREETYPE_GL_USE_VAO
    self->VAO_id = 0;
#endif

    gcvector_init(self->vertices,0,stride,NULL);
    self->vertices_id  = 0;
    self->GPU_vsize = 0;

    self->indices = NULL;
    self->indices_id  = 0;
    self->GPU_isize = 0;

    self->items = NULL;
    self->state = DIRTY;
    self->mode = GL_TRIANGLES;
    return self;
}



// ----------------------------------------------------------------------------
void
vertex_buffer_delete( vertex_buffer_t *self )
{
    size_t i;

    assert( self );

    for( i=0; i<MAX_VERTEX_ATTRIBUTE; ++i )
    {
        if( self->attributes[i] )
        {
            vertex_attribute_delete( self->attributes[i] );
        }
    }

#ifdef FREETYPE_GL_USE_VAO
    if( self->VAO_id )
    {
        glDeleteVertexArrays( 1, &self->VAO_id );
    }
    self->VAO_id = 0;
#endif

    gcvector_deinit(self->vertices);
    if( self->vertices_id )
    {
        glDeleteBuffers( 1, &self->vertices_id );
    }
    self->vertices_id = 0;

    cvector_free( self->indices );
    self->indices = 0;
    if( self->indices_id )
    {
        glDeleteBuffers( 1, &self->indices_id );
    }
    self->indices_id = 0;

    cvector_free( self->items );

    if( self->format )
    {
        free( self->format );
    }
    self->format = 0;
    self->state = 0;
    free( self );
}


// ----------------------------------------------------------------------------
const char *
vertex_buffer_format( const vertex_buffer_t *self )
{
    assert( self );

    return self->format;
}


// ----------------------------------------------------------------------------
size_t
vertex_buffer_size( const vertex_buffer_t *self )
{
    assert( self );

    return cvector_size( self->items );
}


// ----------------------------------------------------------------------------
void
vertex_buffer_print( vertex_buffer_t * self )
{
    int i = 0;
    static char *gltypes[9] = {
        "GL_BOOL",
        "GL_BYTE",
        "GL_UNSIGNED_BYTE",
        "GL_SHORT",
        "GL_UNSIGNED_SHORT",
        "GL_INT",
        "GL_UNSIGNED_INT",
        "GL_FLOAT",
        "GL_VOID"
    };

    assert(self);

    log_error( "%ld vertices, %ld indices\n",
             gcvector_size( self->vertices ), cvector_size( self->indices ) );
    while( self->attributes[i] )
    {
        int j = 8;
        switch( self->attributes[i]->type )
        {
        case GL_BOOL:           j=0; break;
        case GL_BYTE:           j=1; break;
        case GL_UNSIGNED_BYTE:  j=2; break;
        case GL_SHORT:          j=3; break;
        case GL_UNSIGNED_SHORT: j=4; break;
        case GL_INT:            j=5; break;
        case GL_UNSIGNED_INT:   j=6; break;
        case GL_FLOAT:          j=7; break;
        default:                j=8; break;
        }
        log_error( "%s : %dx%s (+%p)\n",
                self->attributes[i]->name,
                self->attributes[i]->size,
                gltypes[j],
                self->attributes[i]->pointer);

        i += 1;
    }
}


// ----------------------------------------------------------------------------
void
vertex_buffer_upload ( vertex_buffer_t *self )
{
    size_t vsize, isize;

    if( self->state == FROZEN )
    {
        return;
    }

    if( !self->vertices_id )
    {
        glGenBuffers( 1, &self->vertices_id );
    }
    if( !self->indices_id )
    {
        glGenBuffers( 1, &self->indices_id );
    }

    vsize = gcvector_size(self->vertices) * gcvector_item_size(self->vertices);
    isize = cvector_size(self->indices) * sizeof(GLuint);


    // Always upload vertices first such that indices do not point to non
    // existing data (if we get interrupted in between for example).

    // Upload vertices
    glBindBuffer( GL_ARRAY_BUFFER, self->vertices_id );
    if( vsize != self->GPU_vsize )
    {
        glBufferData( GL_ARRAY_BUFFER,
                      vsize, self->vertices.data, GL_DYNAMIC_DRAW );
        self->GPU_vsize = vsize;
    }
    else
    {
        glBufferSubData( GL_ARRAY_BUFFER,
                         0, vsize, self->vertices.data );
    }
    glBindBuffer( GL_ARRAY_BUFFER, 0 );

    // Upload indices
    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, self->indices_id );
    if( isize != self->GPU_isize )
    {
        glBufferData( GL_ELEMENT_ARRAY_BUFFER,
                      isize, self->indices, GL_DYNAMIC_DRAW );
        self->GPU_isize = isize;
    }
    else
    {
        glBufferSubData( GL_ELEMENT_ARRAY_BUFFER,
                         0, isize, self->indices );
    }
    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
}



// ----------------------------------------------------------------------------
void
vertex_buffer_clear( vertex_buffer_t *self )
{
    assert( self );

    self->state = FROZEN;
    cvector_clear( self->indices );
    gcvector_clear( self->vertices );
    cvector_clear( self->items );
    self->state = DIRTY;
}



// ----------------------------------------------------------------------------
void
vertex_buffer_render_setup ( vertex_buffer_t *self, GLenum mode )
{
    size_t i;

#ifdef FREETYPE_GL_USE_VAO
    // Unbind so no existing VAO-state is overwritten,
    // (e.g. the GL_ELEMENT_ARRAY_BUFFER-binding).
    glBindVertexArray( 0 );
#endif

    if( self->state != CLEAN )
    {
        vertex_buffer_upload( self );
        self->state = CLEAN;
    }

#ifdef FREETYPE_GL_USE_VAO
    if( self->VAO_id == 0 )
    {
        // Generate and set up VAO

        glGenVertexArrays( 1, &self->VAO_id );
        glBindVertexArray( self->VAO_id );

        glBindBuffer( GL_ARRAY_BUFFER, self->vertices_id );

        for( i=0; i<MAX_VERTEX_ATTRIBUTE; ++i )
        {
            vertex_attribute_t *attribute = self->attributes[i];
            if( attribute == 0 )
            {
                continue;
            }
            else
            {
                vertex_attribute_enable( attribute );
            }
        }

        glBindBuffer( GL_ARRAY_BUFFER, 0 );

        if( self->indices->size )
        {
            glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, self->indices_id );
        }
    }

    // Bind VAO for drawing
    glBindVertexArray( self->VAO_id );
#else

    glBindBuffer( GL_ARRAY_BUFFER, self->vertices_id );

    for( i=0; i<MAX_VERTEX_ATTRIBUTE; ++i )
    {
        vertex_attribute_t *attribute = self->attributes[i];
        if ( attribute == 0 )
        {
            continue;
        }
        else
        {
            vertex_attribute_enable( attribute );
        }
    }

    if( cvector_size(self->indices) )
    {
        glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, self->indices_id );
    }
#endif

    self->mode = mode;
}

// ----------------------------------------------------------------------------
void
vertex_buffer_render_finish ( vertex_buffer_t *self )
{
#ifdef FREETYPE_GL_USE_VAO
    glBindVertexArray( 0 );
#else
    int i;

    for( i=0; i<MAX_VERTEX_ATTRIBUTE; ++i )
    {
        vertex_attribute_t *attribute = self->attributes[i];
        if( attribute == 0 )
        {
            continue;
        }
        else
        {
            glDisableVertexAttribArray( attribute->index );
        }
    }

    glBindBuffer( GL_ARRAY_BUFFER, 0 );
    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
#endif
}


// ----------------------------------------------------------------------------
void
vertex_buffer_render_item ( vertex_buffer_t *self,
                            size_t index )
{
    struct vec4i * item = cvector_at( self->items, index );
    assert( self );
    assert( index < cvector_size( self->items ) );


    if( cvector_size(self->indices) )
    {
        size_t start = item->istart;
        size_t count = item->icount;
        glDrawElements( self->mode, count, GL_UNSIGNED_INT, (void *)(start*sizeof(GLuint)) );
    }
    else if( gcvector_size(self->vertices) )
    {
        size_t start = item->vstart;
        size_t count = item->vcount;
        glDrawArrays( self->mode, start* gcvector_item_size(self->vertices), count);
    }
}


// ----------------------------------------------------------------------------
void
vertex_buffer_render ( vertex_buffer_t *self, GLenum mode )
{
    size_t vcount = gcvector_size(self->vertices);
    size_t icount = cvector_size(self->indices);

    vertex_buffer_render_setup( self, mode );
    if( icount )
    {
        glDrawElements( mode, icount, GL_UNSIGNED_INT, 0 );
    }
    else
    {
        glDrawArrays( mode, 0, vcount );
    }
    vertex_buffer_render_finish( self );
}



// ----------------------------------------------------------------------------
void
vertex_buffer_push_back_indices ( vertex_buffer_t * self,
                                  const GLuint * indices,
                                  const size_t icount )
{
    assert( self );

    self->state |= DIRTY;
    cvector_push_back_data( self->indices, indices, icount );
}



// ----------------------------------------------------------------------------
void
vertex_buffer_push_back_vertices ( vertex_buffer_t * self,
                                   const void * vertices,
                                   const size_t vcount )
{
    assert( self );

    self->state |= DIRTY;
    gcvector_push_back_data( self->vertices, vertices, vcount );
}



// ----------------------------------------------------------------------------
void
vertex_buffer_insert_indices ( vertex_buffer_t *self,
                               const size_t index,
                               const GLuint *indices,
                               const size_t count )
{
    assert( self );
    assert( self->indices );
    assert( index < cvector_size( self->indices ) );

    self->state |= DIRTY;
    cvector_insert_data( self->indices, index, indices, count );
}



// ----------------------------------------------------------------------------
void
vertex_buffer_insert_vertices( vertex_buffer_t *self,
                               const size_t index,
                               const void *vertices,
                               const size_t vcount )
{
    size_t i;
    assert( self );
    assert( index < gcvector_size(self->vertices)+1 );

    self->state |= DIRTY;

     for( i=0; i<cvector_size(self->indices); ++i )
    {
        if( *(GLuint *)(cvector_at( self->indices, i )) > index )
        {
            *(GLuint *)(cvector_at( self->indices, i )) += index;
        }
    }

    gcvector_insert_data( self->vertices, index, vertices, vcount );
}



// ----------------------------------------------------------------------------
void
vertex_buffer_erase_indices( vertex_buffer_t *self,
                             const size_t first,
                             const size_t last )
{
    assert( self );
    assert( self->indices );
    assert( first < cvector_size(self->indices) );
    assert( (last) <= cvector_size(self->indices) );

    self->state |= DIRTY;
    cvector_erase_range( self->indices, first, last );
}



// ----------------------------------------------------------------------------
void
vertex_buffer_erase_vertices( vertex_buffer_t *self,
                              const size_t first,
                              const size_t last )
{
    size_t i;
    assert( self );
    assert( first < gcvector_size(self->vertices) );
    assert( last <= gcvector_size(self->vertices) );
    assert( last > first );

    self->state |= DIRTY;
    for( i=0; i<gcvector_size(self->vertices); ++i )
    {
        if( *(GLuint *)(cvector_at( self->indices, i )) > first )
        {
            *(GLuint *)(cvector_at( self->indices, i )) -= (last-first);
        }
    }
    gcvector_erase_range( self->vertices, first, last );
}



// ----------------------------------------------------------------------------
size_t
vertex_buffer_push_back( vertex_buffer_t * self,
                         const void * vertices, const size_t vcount,
                         const GLuint * indices, const size_t icount )
{
    return vertex_buffer_insert( self, cvector_size( self->items ),
                                 vertices, vcount, indices, icount );
}

// ----------------------------------------------------------------------------
size_t
vertex_buffer_insert( vertex_buffer_t * self, const size_t index,
                      const void * vertices, const size_t vcount,
                      const GLuint * indices, const size_t icount )
{
    size_t vstart, istart, i;
    struct vec4i item;
    assert( self );
    assert( vertices );
    assert( indices );

    self->state = FROZEN;

    // Push back vertices
    vstart = gcvector_size( self->vertices );
    vertex_buffer_push_back_vertices( self, vertices, vcount );

    // Push back indices
    istart = cvector_size( self->indices );
    vertex_buffer_push_back_indices( self, indices, icount );

    // Update indices within the vertex buffer
    for( i=0; i<icount; ++i )
    {
        *(GLuint *)(cvector_at( self->indices, istart+i )) += vstart;
    }

    // Insert item
    item.x = vstart;
    item.y = vcount;
    item.z = istart;
    item.w = icount;
    cvector_insert( self->items, index, item );

    self->state = DIRTY;
    return index;
}

// ----------------------------------------------------------------------------
void
vertex_buffer_erase( vertex_buffer_t * self,
                     const size_t index )
{
    struct vec4i * item;
    int vstart;
    size_t vcount, istart, icount, i;

    assert( self );
    assert( index < cvector_size( self->items ) );

    item = (struct vec4i *) cvector_at( self->items, index );
    vstart = item->vstart;
    vcount = item->vcount;
    istart = item->istart;
    icount = item->icount;

    // Update items
    for( i=0; i<cvector_size(self->items); ++i )
    {
        struct vec4i * item = (struct vec4i *) cvector_at( self->items, i );
        if( item->vstart > vstart)
        {
            item->vstart -= vcount;
            item->istart -= icount;
        }
    }

    self->state = FROZEN;
    vertex_buffer_erase_indices( self, istart, istart+icount );
    vertex_buffer_erase_vertices( self, vstart, vstart+vcount );
    cvector_erase( self->items, index );
    self->state = DIRTY;
}
