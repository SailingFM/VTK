/*=========================================================================

  Program:   Visualization Toolkit
  Module:    vtkOpenGLProperty.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "vtkOpenGLRenderer.h"
#include "vtkOpenGL2Property.h"

#include "vtkOpenGL.h"

#include "vtkObjectFactory.h"
#include "vtkOpenGLExtensionManager.h"
#include "vtkOpenGLTexture.h"
#include "vtkTexture.h"

#include "vtkgl.h" // vtkgl namespace

#include "vtkTextureUnitManager.h"
#include "vtkOpenGLRenderWindow.h"
#include "vtkOpenGLError.h"

#include <cassert>

namespace
{
  void ComputeMaterialColor(GLfloat result[4],
    bool premultiply_colors_with_alpha,
    double color_factor,
    const double color[3],
    double opacity)
    {
    double opacity_factor = premultiply_colors_with_alpha? opacity : 1.0;
    for (int cc=0; cc < 3; cc++)
      {
      result[cc] = static_cast<GLfloat>(
        opacity_factor * color_factor * color[cc]);
      }
    result[3] = opacity;
    }
}

vtkStandardNewMacro(vtkOpenGL2Property);

vtkOpenGL2Property::vtkOpenGL2Property()
{
}

vtkOpenGL2Property::~vtkOpenGL2Property()
{
}


// ----------------------------------------------------------------------------
// Implement base class method.
void vtkOpenGL2Property::Render(vtkActor *anActor, vtkRenderer *ren)
{
  vtkOpenGLRenderWindow* context = vtkOpenGLRenderWindow::SafeDownCast(
      ren->GetRenderWindow());

  if (!context)
    {
    // must be an OpenGL context.
    return;
    }

  // Set the PointSize
  glPointSize(this->PointSize);
  //vtkOpenGLGL2PSHelper::SetPointSize(this->PointSize);

  // Set the LineWidth
  glLineWidth(this->LineWidth);
  //vtkOpenGLGL2PSHelper::SetLineWidth(this->LineWidth);

  // Set the LineStipple
  if (this->LineStipplePattern != 0xFFFF)
    {
    glEnable(GL_LINE_STIPPLE);
    glLineStipple(this->LineStippleRepeatFactor,
                  static_cast<GLushort>(this->LineStipplePattern));
    //vtkOpenGLGL2PSHelper::EnableStipple(); // must be called after glLineStipple
    }
  else
    {
    // still need to set this although we are disabling.  else the ATI X1600
    // (for example) still manages to stipple under certain conditions.
    glLineStipple(this->LineStippleRepeatFactor,
                  static_cast<GLushort>(this->LineStipplePattern));
    glDisable(GL_LINE_STIPPLE);
    //vtkOpenGLGL2PSHelper::DisableStipple();
    }

  glDisable(GL_TEXTURE_2D); // fixed-pipeline

  // disable alpha testing (this may have been enabled
  // by another actor in OpenGLTexture)
  glDisable (GL_ALPHA_TEST);

  // turn on/off backface culling
  if (! this->BackfaceCulling && ! this->FrontfaceCulling)
    {
    glDisable (GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
  else if (this->BackfaceCulling)
    {
    glCullFace (GL_BACK);
    glEnable (GL_CULL_FACE);
    }
  else //if both front & back culling on, will fall into backface culling
    { //if you really want both front and back, use the Actor's visibility flag
    glCullFace (GL_FRONT);
    glEnable (GL_CULL_FACE);
    }

  this->RenderTextures(anActor, ren);
  this->Superclass::Render(anActor, ren);
}

//-----------------------------------------------------------------------------
bool vtkOpenGL2Property::RenderTextures(vtkActor*, vtkRenderer* ren)
{
  vtkOpenGLRenderWindow* context = vtkOpenGLRenderWindow::SafeDownCast(
      ren->GetRenderWindow());
  assert(context!=NULL);

  // render any textures.
  int numTextures = this->GetNumberOfTextures();
  if (numTextures > 0)
    {
    if (true) // fixed-pipeline multitexturing or old XML shaders.
      {
      this->LoadMultiTexturingExtensions(ren);
      if (vtkgl::ActiveTexture)
        {
        GLint numSupportedTextures;
        glGetIntegerv(vtkgl::MAX_TEXTURE_UNITS, &numSupportedTextures);
        for (int t = 0; t < numTextures; t++)
          {
          int texture_unit = this->GetTextureUnitAtIndex(t);
          if (texture_unit >= numSupportedTextures || texture_unit < 0)
            {
            vtkErrorMacro("Hardware does not support the number of textures defined.");
            continue;
            }

          vtkgl::ActiveTexture(vtkgl::TEXTURE0 +
                               static_cast<GLenum>(texture_unit));
          this->GetTextureAtIndex(t)->Render(ren);
          }
        vtkgl::ActiveTexture(vtkgl::TEXTURE0);
        }
      else
        {
        this->GetTextureAtIndex(0)->Render(ren); // one-texture fixed-pipeline
        }
      }
    else
      {
      // texture unit are assigned at each call to render, as render can
      // happen in different/multiple passes.

      vtkTextureUnitManager *m = context->GetTextureUnitManager();
      for (int t = 0; t < numTextures; t++)
        {
        vtkTexture *tex = this->GetTextureAtIndex(t);
        int unit = m->Allocate();
        if (unit == -1)
          {
          vtkErrorMacro(<<" not enough texture units.");
          return false;
          }
        this->SetTexture(unit,tex);
        vtkgl::ActiveTexture(vtkgl::TEXTURE0 + static_cast<GLenum>(unit));
        // bind (and load if not yet loaded)
        tex->Render(ren);
        }
      vtkgl::ActiveTexture(vtkgl::TEXTURE0);
      }
    }

  vtkOpenGLCheckErrorMacro("failed after Render");

  return (numTextures > 0);
}

//-----------------------------------------------------------------------------
void vtkOpenGL2Property::PostRender(vtkActor *actor, vtkRenderer *renderer)
{
  vtkOpenGLClearErrorMacro();

  // Reset the face culling now we are done, leaking into text actor etc.
  if (this->BackfaceCulling || this->FrontfaceCulling)
    {
    glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

  this->Superclass::PostRender(actor, renderer);

  // render any textures.
  int numTextures = this->GetNumberOfTextures();
  if (numTextures > 0 && vtkgl::ActiveTexture)
    {
    if (true) // fixed-pipeline multitexturing or old XML shaders.
      {
      GLint numSupportedTextures;
      glGetIntegerv(vtkgl::MAX_TEXTURE_UNITS, &numSupportedTextures);
      for (int i = 0; i < numTextures; i++)
        {
        int texture_unit = this->GetTextureUnitAtIndex(i);
        if (texture_unit >= numSupportedTextures || texture_unit < 0)
          {
          vtkErrorMacro("Hardware does not support the number of textures defined.");
          continue;
          }
        vtkgl::ActiveTexture(vtkgl::TEXTURE0+
                             static_cast<GLenum>(texture_unit));
        // Disable any possible texture.  Wouldn't having a PostRender on
        // vtkTexture be better?
        glDisable(GL_TEXTURE_1D);
        glDisable(GL_TEXTURE_2D);
        glDisable(vtkgl::TEXTURE_3D);
        glDisable(vtkgl::TEXTURE_RECTANGLE_ARB);
        glDisable(vtkgl::TEXTURE_CUBE_MAP);
        }
      vtkgl::ActiveTexture(vtkgl::TEXTURE0);
      }
    else
      {
      vtkTextureUnitManager* m =
        static_cast<vtkOpenGLRenderWindow *>(renderer->GetRenderWindow())->GetTextureUnitManager();

      for (int t = 0; t < numTextures; t++)
        {
        int textureUnit=this->GetTextureUnitAtIndex(t);
        m->Free(textureUnit);
        }
      vtkgl::ActiveTexture(vtkgl::TEXTURE0);
      }
    }

  vtkOpenGLCheckErrorMacro("failed after PostRender");
}

//-----------------------------------------------------------------------------
// Implement base class method.
void vtkOpenGL2Property::BackfaceRender(vtkActor *vtkNotUsed(anActor), vtkRenderer *ren)
{
}

//-----------------------------------------------------------------------------
void vtkOpenGL2Property::LoadMultiTexturingExtensions(vtkRenderer* ren)
{
  if (!vtkgl::MultiTexCoord2d || !vtkgl::ActiveTexture)
    {
    vtkOpenGLExtensionManager* extensions = vtkOpenGLExtensionManager::New();
    extensions->SetRenderWindow(ren->GetRenderWindow());

    // multitexture is a core feature of OpenGL 1.3.
    // multitexture is an ARB extension of OpenGL 1.2.1
    int supports_GL_1_3 = extensions->ExtensionSupported( "GL_VERSION_1_3" );
    int supports_GL_1_2_1 = extensions->ExtensionSupported("GL_VERSION_1_2");
    int supports_ARB_mutlitexture = extensions->ExtensionSupported(
        "GL_ARB_multitexture");

    if (supports_GL_1_3)
      {
      extensions->LoadExtension("GL_VERSION_1_3");
      }
    else if(supports_GL_1_2_1 && supports_ARB_mutlitexture)
      {
      extensions->LoadExtension("GL_VERSION_1_2");
      extensions->LoadCorePromotedExtension("GL_ARB_multitexture");
      }
    extensions->Delete();
    }
}

//-----------------------------------------------------------------------------
void vtkOpenGL2Property::ReleaseGraphicsResources(vtkWindow *win)
{
  // release any textures.
  int numTextures = this->GetNumberOfTextures();
  if (win && win->GetMapped() && numTextures > 0 && vtkgl::ActiveTexture)
    {
    vtkOpenGLClearErrorMacro();
    GLint numSupportedTextures;
    glGetIntegerv(vtkgl::MAX_TEXTURE_UNITS, &numSupportedTextures);
    for (int i = 0; i < numTextures; i++)
      {
      if (vtkOpenGLTexture::SafeDownCast(this->GetTextureAtIndex(i))->GetIndex() == 0)
        {
        continue;
        }
      int texture_unit = this->GetTextureUnitAtIndex(i);
      if (texture_unit >= numSupportedTextures || texture_unit < 0)
        {
        vtkErrorMacro("Hardware does not support the texture unit " << texture_unit << ".");
        continue;
        }
      vtkgl::ActiveTexture(vtkgl::TEXTURE0 +
                           static_cast<GLenum>(texture_unit));
      this->GetTextureAtIndex(i)->ReleaseGraphicsResources(win);
      }
    vtkgl::ActiveTexture(vtkgl::TEXTURE0);
    vtkOpenGLCheckErrorMacro("failwed during ReleaseGraphicsResources");
    }
  else if (numTextures > 0 && vtkgl::ActiveTexture)
    {
    for (int i = 0; i < numTextures; i++)
      {
      this->GetTextureAtIndex(i)->ReleaseGraphicsResources(win);
      }
    }

  this->Superclass::ReleaseGraphicsResources(win);

}

//----------------------------------------------------------------------------
void vtkOpenGL2Property::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os,indent);

}
