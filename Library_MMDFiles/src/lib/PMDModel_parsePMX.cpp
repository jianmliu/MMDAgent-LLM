/* ----------------------------------------------------------------- */
/*           Toolkit for Building Voice Interaction Systems          */
/*           MMDAgent developed by MMDAgent Project Team             */
/*           http://www.mmdagent.jp/                                 */
/* ----------------------------------------------------------------- */
/*                                                                   */
/*  Copyright (c) 2009-2016  Nagoya Institute of Technology          */
/*                           Department of Computer Science          */
/*                                                                   */
/* All rights reserved.                                              */
/*                                                                   */
/* Redistribution and use in source and binary forms, with or        */
/* without modification, are permitted provided that the following   */
/* conditions are met:                                               */
/*                                                                   */
/* - Redistributions of source code must retain the above copyright  */
/*   notice, this list of conditions and the following disclaimer.   */
/* - Redistributions in binary form must reproduce the above         */
/*   copyright notice, this list of conditions and the following     */
/*   disclaimer in the documentation and/or other materials provided */
/*   with the distribution.                                          */
/* - Neither the name of the MMDAgent project team nor the names of  */
/*   its contributors may be used to endorse or promote products     */
/*   derived from this software without specific prior written      */
/*   permission.                                                     */
/*                                                                   */
/* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND            */
/* CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,       */
/* INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF          */
/* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE          */
/* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS */
/* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,          */
/* EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED   */
/* TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,     */
/* DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON */
/* ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,   */
/* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY    */
/* OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE           */
/* POSSIBILITY OF SUCH DAMAGE.                                       */
/* ----------------------------------------------------------------- */

/* Native PMX (2.0 / 2.1) loader.

   Reads a .pmx file into the same structures that the PMD parser and the PMXEditor CSV
   extension (parseExtCsv) fill, so a PMX model behaves as its PMD + CSV conversion would:
   base data goes through the PMD setup functions (PMDFile_* records built in memory, in
   PMD's left-handed coordinates), and PMX-only data (4-bone / SDEF skinning, bone
   inheritance and process layers, bone / vertex / UV / material / group morphs, material
   flags, edge colors and full texture paths) goes through the same setters parseExtCsv uses.

   Not supported (skipped): additional UVs, QDEF (loaded as BDEF4), fixed / local axes,
   external parents, flip and impulse morphs, joints other than spring 6DOF (loaded as
   spring 6DOF), soft bodies. */

#include "MMDFiles.h"

#include <vector>
#include <string>

/* PMX bone flags */
#define PMX_BONE_TAIL_IS_BONE   0x0001
#define PMX_BONE_MOVE           0x0004
#define PMX_BONE_VISIBLE        0x0008
#define PMX_BONE_IK             0x0020
#define PMX_BONE_INHERIT_ROT    0x0100
#define PMX_BONE_INHERIT_MOVE   0x0200
#define PMX_BONE_FIXED_AXIS     0x0400
#define PMX_BONE_LOCAL_AXES     0x0800
#define PMX_BONE_AFTER_PHYSICS  0x1000
#define PMX_BONE_EXTERNAL       0x2000

/* PMX material flags */
#define PMX_MAT_DOUBLE_SIDED    0x01
#define PMX_MAT_GROUND_SHADOW   0x02
#define PMX_MAT_SELF_SHADOW_MAP 0x04
#define PMX_MAT_SELF_SHADOW     0x08
#define PMX_MAT_EDGE            0x10

namespace {

/* PMXReader: bounds-checked little-endian reader */
struct PMXReader {
   const unsigned char *p;
   const unsigned char *end;
   bool ok;
   int encoding; /* 0: UTF-16LE, 1: UTF-8 */

   PMXReader(const unsigned char *data, unsigned long size) : p(data), end(data + size), ok(true), encoding(0) {}

   bool need(size_t n) {
      if (!ok || (size_t)(end - p) < n) {
         ok = false;
         return false;
      }
      return true;
   }
   void skip(size_t n) {
      if (need(n)) p += n;
   }
   unsigned char u8() {
      if (!need(1)) return 0;
      return *p++;
   }
   unsigned short u16() {
      unsigned short v = 0;
      if (need(2)) { memcpy(&v, p, 2); p += 2; }
      return v;
   }
   int i32() {
      int v = 0;
      if (need(4)) { memcpy(&v, p, 4); p += 4; }
      return v;
   }
   float f32() {
      float v = 0.0f;
      if (need(4)) { memcpy(&v, p, 4); p += 4; }
      return v;
   }
   void vec(float *v, int n) {
      for (int i = 0; i < n; i++) v[i] = f32();
   }
   /* count: non-negative 32-bit count of items taking at least minBytes each; a count the
      rest of the file cannot hold is an error, so a crafted file can't force huge allocations */
   int count(size_t minBytes = 1) {
      int n = i32();
      if (n < 0 || (minBytes > 0 && (size_t)n > (size_t)(end - p) / minBytes)) { ok = false; return 0; }
      return n;
   }
   /* idx: signed index of 1, 2 or 4 bytes (-1 = none) */
   int idx(int size) {
      if (size == 1) { signed char v = (signed char)u8(); return v; }
      if (size == 2) { short v = (short)u16(); return v; }
      return i32();
   }
   /* vidx: vertex index, unsigned for 1 and 2 bytes */
   int vidx(int size) {
      if (size == 1) return u8();
      if (size == 2) return u16();
      return i32();
   }
   /* text: length-prefixed string, returned as UTF-8 */
   std::string text() {
      int n = count();
      std::string out;
      if (!need((size_t)n)) return out;
      if (encoding == 1) {
         out.assign((const char *)p, n);
      } else {
         /* UTF-16LE -> UTF-8 */
         for (int i = 0; i + 1 < n; i += 2) {
            unsigned int c = p[i] | (p[i + 1] << 8);
            if (c >= 0xD800 && c <= 0xDBFF && i + 3 < n) {
               unsigned int c2 = p[i + 2] | (p[i + 3] << 8);
               if (c2 >= 0xDC00 && c2 <= 0xDFFF) {
                  c = 0x10000 + ((c - 0xD800) << 10) + (c2 - 0xDC00);
                  i += 2;
               }
            }
            if (c < 0x80) {
               out += (char)c;
            } else if (c < 0x800) {
               out += (char)(0xC0 | (c >> 6));
               out += (char)(0x80 | (c & 0x3F));
            } else if (c < 0x10000) {
               out += (char)(0xE0 | (c >> 12));
               out += (char)(0x80 | ((c >> 6) & 0x3F));
               out += (char)(0x80 | (c & 0x3F));
            } else {
               out += (char)(0xF0 | (c >> 18));
               out += (char)(0x80 | ((c >> 12) & 0x3F));
               out += (char)(0x80 | ((c >> 6) & 0x3F));
               out += (char)(0x80 | (c & 0x3F));
            }
         }
      }
      p += n;
      return out;
   }
};

/* PMXIKLink: IK link of a PMX bone */
struct PMXIKLink {
   int bone;
   bool hasLimit;
   float min[3];
   float max[3];
};

/* PMXBone: PMX bone record */
struct PMXBone {
   std::string name;
   float pos[3];
   int parent;
   int layer;
   unsigned short flags;
   int tail;
   int inheritParent;
   float inheritRate;
   int ikTarget;
   int ikLoops;
   float ikLimit;
   std::vector<PMXIKLink> ikLinks;
};

/* pathOf: texture path with the platform's directory separator */
std::string pathOf(const std::string &s)
{
   std::string out = s;
#ifndef _WIN32
   for (size_t i = 0; i < out.size(); i++)
      if (out[i] == '\\') out[i] = '/';
#endif /* !_WIN32 */
   return out;
}

/* dupOrNull: strdup of a non-empty string */
char *dupOrNull(const std::string &s)
{
   return s.empty() ? NULL : MMDFiles_strdup(s.c_str());
}

} /* namespace */

/* PMDModel::parsePMX: initialize and load from PMX data in memory */
bool PMDModel::parsePMX(const unsigned char *data, unsigned long size, BulletPhysics *bullet, SystemTexture *systex, const char *dir)
{
   PMXReader r(data, size);
   btQuaternion defaultRot(btScalar(0.0f), btScalar(0.0f), btScalar(0.0f), btScalar(1.0f));
   unsigned int i, j;
   unsigned char globals[8];
   int sizeVertex, sizeTexture, sizeMaterial, sizeBone, sizeMorph, sizeRigid;
   int numAdditionalUV;
   float version;
   char buf[MMDFILES_MAXBUFLEN];

   /* clear memory */
   clear();
   m_hasSingleSphereMap = false;
   m_hasMultipleSphereMap = false;
   m_baseFace = NULL;
   m_centerBone = NULL;

   /* reset root bone's rotation */
   m_rootBone.setCurrentRotation(&defaultRot);
   m_rootBone.update();

   /* set Bullet Physics */
   m_bulletPhysics = bullet;

   /* reset toon texture IDs by system default textures */
   for (j = 0; j < SYSTEMTEXTURE_NUMFILES; j++)
      m_toonTextureID[j] = systex->getTextureID(j);

   /* header */
   if (size < 9 || data[0] != 'P' || data[1] != 'M' || data[2] != 'X' || data[3] != ' ')
      return false;
   r.skip(4);
   version = r.f32();
   if (version < 2.0f)
      return false;
   {
      unsigned char n = r.u8();
      if (n < 8)
         return false;
      for (j = 0; j < n; j++) {
         unsigned char g = r.u8();
         if (j < 8) globals[j] = g;
      }
   }
   r.encoding = globals[0];
   numAdditionalUV = globals[1];
   sizeVertex = globals[2];
   sizeTexture = globals[3];
   sizeMaterial = globals[4];
   sizeBone = globals[5];
   sizeMorph = globals[6];
   sizeRigid = globals[7];
   if (r.encoding > 1 || numAdditionalUV > 4)
      return false;
   for (j = 2; j < 8; j++)
      if (globals[j] != 1 && globals[j] != 2 && globals[j] != 4)
         return false;

   /* names and comments */
   m_name = MMDFiles_strdup(r.text().c_str());
   r.text(); /* English name */
   m_comment = MMDFiles_strdup(r.text().c_str());
   r.text(); /* English comment */
   if (!r.ok) return false;

   /* vertices: positions, normals, uv, skinning, edge scale */
   m_numVertex = (unsigned int)r.count(33 + 16 * numAdditionalUV);
   if (!r.ok) return false;
   m_vertexList = (btVector3 *)MMDFiles_alignedmalloc(sizeof(btVector3) * m_numVertex, 16);
   m_normalList = (btVector3 *)MMDFiles_alignedmalloc(sizeof(btVector3) * m_numVertex, 16);
   m_texCoordList = (TexCoord *)malloc(sizeof(TexCoord) * m_numVertex);
   m_bone1List = (short *)malloc(sizeof(short) * m_numVertex);
   m_bone2List = (short *)malloc(sizeof(short) * m_numVertex);
   m_boneWeight1 = (float *)malloc(sizeof(float) * m_numVertex);
   m_edgeWidth = (float *)malloc(sizeof(float) * m_numVertex);
#ifdef MY_EXTRADEFORMATION
   /* 4-bone and SDEF skinning, as parseExtCsv sets them up */
   m_bone3List = (short *)malloc(sizeof(short) * m_numVertex);
   m_bone4List = (short *)malloc(sizeof(short) * m_numVertex);
   m_boneWeight2 = (float *)malloc(sizeof(float) * m_numVertex);
   m_boneWeight3 = (float *)malloc(sizeof(float) * m_numVertex);
   m_boneWeight4 = (float *)malloc(sizeof(float) * m_numVertex);
   m_sdefC = (btVector3 *)MMDFiles_alignedmalloc(sizeof(btVector3) * m_numVertex, 16);
   m_sdefR0 = (btVector3 *)MMDFiles_alignedmalloc(sizeof(btVector3) * m_numVertex, 16);
   m_sdefR1 = (btVector3 *)MMDFiles_alignedmalloc(sizeof(btVector3) * m_numVertex, 16);
#endif /* MY_EXTRADEFORMATION */
   for (i = 0; i < m_numVertex && r.ok; i++) {
      float v[4];
      int b[4] = { 0, 0, 0, 0 };
      float w[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
      unsigned char type;

      r.vec(v, 3);
      m_vertexList[i].setValue(btScalar(v[0]), btScalar(v[1]), btScalar(v[2]));
      r.vec(v, 3);
      m_normalList[i].setValue(btScalar(v[0]), btScalar(v[1]), btScalar(v[2]));
      r.vec(v, 2);
      m_texCoordList[i].u = v[0];
      m_texCoordList[i].v = v[1];
      r.skip(16 * numAdditionalUV);
      type = r.u8();
#ifdef MY_EXTRADEFORMATION
      m_bone3List[i] = m_bone4List[i] = -1;
      m_sdefC[i].setZero();
      m_sdefR0[i].setZero();
      m_sdefR1[i].setZero();
#endif /* MY_EXTRADEFORMATION */
      switch (type) {
      case 0: /* BDEF1 */
         b[0] = b[1] = r.idx(sizeBone);
         break;
      case 1: /* BDEF2 */
         b[0] = r.idx(sizeBone);
         b[1] = r.idx(sizeBone);
         w[0] = r.f32();
         w[1] = 1.0f - w[0];
         break;
      case 2: /* BDEF4 */
      case 4: /* QDEF (PMX 2.1), loaded as BDEF4 */
         for (j = 0; j < 4; j++) b[j] = r.idx(sizeBone);
         r.vec(w, 4);
         break;
      case 3: { /* SDEF */
         float c[3], r0[3], r1[3];
         b[0] = r.idx(sizeBone);
         b[1] = r.idx(sizeBone);
         w[0] = r.f32();
         w[1] = 1.0f - w[0];
         r.vec(c, 3);
         r.vec(r0, 3);
         r.vec(r1, 3);
#ifdef MY_EXTRADEFORMATION
         /* same workaround as parseExtCsv against vanishing vertices on degenerate SDEF */
         if (c[0] != r0[0] || c[1] != r0[1] || c[2] != r0[2] || c[0] != r1[0] || c[1] != r1[1] || c[2] != r1[2]) {
            m_bone3List[i] = -2;
            m_sdefC[i].setValue(btScalar(c[0]), btScalar(c[1]), btScalar(c[2]));
            m_sdefR0[i].setValue(btScalar(r0[0]), btScalar(r0[1]), btScalar(r0[2]));
            m_sdefR1[i].setValue(btScalar(r1[0]), btScalar(r1[1]), btScalar(r1[2]));
         }
#endif /* MY_EXTRADEFORMATION */
         break;
      }
      default:
         r.ok = false;
         break;
      }
      /* unused slots are -1: point them at bone 0 (their weight is 0); indices past the bone
         list are fixed once the bones are known */
      for (j = 0; j < 4; j++)
         if (b[j] < 0 || b[j] > 32767) b[j] = 0;
      m_bone1List[i] = (short)b[0];
      m_bone2List[i] = (short)b[1];
      m_boneWeight1[i] = w[0];
#ifdef MY_EXTRADEFORMATION
      m_boneWeight2[i] = w[1];
      if (type == 2 || type == 4) {
         m_bone3List[i] = (short)b[2];
         m_bone4List[i] = (short)b[3];
         m_boneWeight3[i] = w[2];
         m_boneWeight4[i] = w[3];
      } else {
         m_boneWeight3[i] = m_boneWeight4[i] = 0.0f;
      }
#endif /* MY_EXTRADEFORMATION */
      m_edgeWidth[i] = r.f32();
   }
   if (!r.ok) return false;

   /* surfaces */
   m_numSurface = (unsigned int)r.count(sizeVertex);
   if (!r.ok) return false;
#ifndef OPENGL_USE_UNSIGNED_INT_FOR_INDICES
   if (m_numVertex > 65535)
      return false;
#endif /* !OPENGL_USE_UNSIGNED_INT_FOR_INDICES */
   m_surfaceList = (INDICES *)malloc(sizeof(INDICES) * m_numSurface);
   for (i = 0; i < m_numSurface; i++) {
      int v = r.vidx(sizeVertex);
      if (v < 0 || (unsigned int)v >= m_numVertex) {
         r.ok = false;
         break;
      }
      m_surfaceList[i] = (INDICES)v;
   }
   if (!r.ok) return false;

   /* texture paths */
   std::vector<std::string> textures;
   {
      int n = r.count(4);
      for (int k = 0; k < n && r.ok; k++)
         textures.push_back(pathOf(r.text()));
   }
   if (!r.ok) return false;

   /* materials */
   m_numMaterial = (unsigned int)r.count(70);
   if (!r.ok) return false;
   m_material = new PMDMaterial[m_numMaterial];
   {
      std::vector<int> customToon; /* per material: texture index of its own toon, or -1 */
      bool sharedToonUsed[SYSTEMTEXTURE_NUMFILES] = { false };
      unsigned int surfaceFrom = 0;

      m_loadingProgressRate = 0.1f;
      for (i = 0; i < m_numMaterial && r.ok; i++) {
         PMDFile_Material fm;
         std::string name;
         float diffuse[4], specular[3], ambient[3], edgeColor[4];
         float shiness, edgeSize;
         unsigned char flags, sphereMode, sharedToon;
         int texture, sphere, toon;

         name = r.text();
         r.text(); /* English name */
         r.vec(diffuse, 4);
         r.vec(specular, 3);
         shiness = r.f32();
         r.vec(ambient, 3);
         flags = r.u8();
         r.vec(edgeColor, 4);
         edgeSize = r.f32();
         texture = r.idx(sizeTexture);
         sphere = r.idx(sizeTexture);
         sphereMode = r.u8();
         sharedToon = r.u8();
         toon = sharedToon ? r.u8() : r.idx(sizeTexture);
         r.text(); /* memo */
         memset(&fm, 0, sizeof(PMDFile_Material));
         for (j = 0; j < 3; j++) {
            fm.diffuse[j] = diffuse[j];
            fm.specular[j] = specular[j];
            fm.ambient[j] = ambient[j];
         }
         fm.alpha = diffuse[3];
         fm.shiness = shiness;
         fm.edgeFlag = (flags & PMX_MAT_EDGE) ? 1 : 0;
         {
            int ns = r.i32();
            /* the materials must split the surface list exactly (checked without overflow) */
            if (ns < 0 || (unsigned int)ns > m_numSurface - surfaceFrom) {
               r.ok = false;
               break;
            }
            fm.numSurfaceIndex = (unsigned int)ns;
         }
         /* toon: shared toon n is toon(n+1).bmp as in PMD; an own toon texture gets a slot below */
         if (sharedToon) {
            fm.toonID = (toon >= 0 && toon < SYSTEMTEXTURE_NUMFILES - 1) ? (unsigned char)toon : 0xff;
            if (fm.toonID != 0xff) sharedToonUsed[fm.toonID + 1] = true;
            customToon.push_back(-1);
         } else {
            fm.toonID = 0xff;
            customToon.push_back((toon >= 0 && (size_t)toon < textures.size()) ? toon : -1);
         }
         if (!r.ok) break;

         /* textures are given by full path through setExtParam, not the 20-byte PMD field */
         m_material[i].setup(&fm, NULL, dir, surfaceFrom);
         m_material[i].setName(name.c_str());
         {
            char *tex = (texture >= 0 && (size_t)texture < textures.size()) ? dupOrNull(textures[texture]) : NULL;
            char *sph = (sphere >= 0 && (size_t)sphere < textures.size()) ? dupOrNull(textures[sphere]) : NULL;
            m_material[i].setExtParam((flags & PMX_MAT_EDGE) ? true : false, edgeSize, edgeColor, diffuse[3],
                                      (flags & PMX_MAT_DOUBLE_SIDED) ? true : false, (flags & PMX_MAT_GROUND_SHADOW) ? true : false,
                                      (flags & PMX_MAT_SELF_SHADOW_MAP) ? true : false, (flags & PMX_MAT_SELF_SHADOW) ? true : false,
                                      tex, sph, sphereMode, dir, &m_textureLoader);
            if (tex) free(tex);
            if (sph) free(sph);
         }
         surfaceFrom += m_material[i].getNumSurface();
         m_loadingProgressRate = 0.1f + (0.7f * i) / m_numMaterial;
      }
      if (!r.ok) return false;
      if (surfaceFrom != m_numSurface)
         return false;

      /* local toon0.bmp - toon10.bmp next to the model replace the system ones, as for PMD */
      for (j = 0; j < SYSTEMTEXTURE_NUMFILES; j++) {
         if (j == 0)
            MMDFiles_snprintf(buf, MMDFILES_MAXBUFLEN, "%s%ctoon0.bmp", dir, MMDFILES_DIRSEPARATOR);
         else
            MMDFiles_snprintf(buf, MMDFILES_MAXBUFLEN, "%s%ctoon%02d.bmp", dir, MMDFILES_DIRSEPARATOR, j);
         if (MMDFiles_exist(buf) && m_localToonTexture[j].load(buf) == true)
            m_toonTextureID[j] = m_localToonTexture[j].getID();
      }
      /* own toon textures: give each distinct one a slot that no shared toon uses */
      {
         std::vector<int> slotTexture(SYSTEMTEXTURE_NUMFILES, -1);
         for (i = 0; i < m_numMaterial; i++) {
            int t = customToon[i];
            int slot = -1;
            if (t < 0) continue;
            for (j = 1; j < SYSTEMTEXTURE_NUMFILES; j++)
               if (slotTexture[j] == t) slot = j;
            if (slot < 0) {
               for (j = SYSTEMTEXTURE_NUMFILES - 1; j >= 1; j--) {
                  if (!sharedToonUsed[j] && slotTexture[j] < 0) {
                     MMDFiles_snprintf(buf, MMDFILES_MAXBUFLEN, "%s%c%s", dir, MMDFILES_DIRSEPARATOR, textures[t].c_str());
                     if (MMDFiles_exist(buf) && m_localToonTexture[j].load(buf) == true) {
                        m_toonTextureID[j] = m_localToonTexture[j].getID();
                        slotTexture[j] = t;
                        slot = j;
                     }
                     break;
                  }
               }
            }
            if (slot > 0)
               m_material[i].setToonID(slot);
         }
      }
      for (j = 0; j < SYSTEMTEXTURE_NUMFILES; j++) {
         if (m_toonTextureID[j] == systex->getTextureID(j)) continue;
         /* set GL_CLAMP_TO_EDGE for toon texture to avoid texture interpolation at edge */
         glBindTexture(GL_TEXTURE_2D, m_toonTextureID[j]);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
         glBindTexture(GL_TEXTURE_2D, 0);
      }
   }
   m_loadingProgressRate = 0.8f;

   /* bones */
   std::vector<PMXBone> bones((size_t)r.count(28));
   if (!r.ok) return false;
   if (bones.size() > 32767)
      return false;
   for (i = 0; i < bones.size() && r.ok; i++) {
      PMXBone &b = bones[i];
      b.name = r.text();
      r.text(); /* English name */
      r.vec(b.pos, 3);
      b.parent = r.idx(sizeBone);
      b.layer = r.i32();
      b.flags = r.u16();
      b.tail = -1;
      if (b.flags & PMX_BONE_TAIL_IS_BONE) {
         b.tail = r.idx(sizeBone);
      } else {
         float offset[3];
         r.vec(offset, 3);
      }
      b.inheritParent = -1;
      b.inheritRate = 0.0f;
      if (b.flags & (PMX_BONE_INHERIT_ROT | PMX_BONE_INHERIT_MOVE)) {
         b.inheritParent = r.idx(sizeBone);
         b.inheritRate = r.f32();
      }
      if (b.flags & PMX_BONE_FIXED_AXIS) r.skip(12);
      if (b.flags & PMX_BONE_LOCAL_AXES) r.skip(24);
      if (b.flags & PMX_BONE_EXTERNAL) r.skip(4);
      b.ikTarget = -1;
      if (b.flags & PMX_BONE_IK) {
         int n;
         b.ikTarget = r.idx(sizeBone);
         b.ikLoops = r.i32();
         b.ikLimit = r.f32();
         n = r.count(sizeBone + 1);
         for (int k = 0; k < n && r.ok; k++) {
            PMXIKLink l;
            l.bone = r.idx(sizeBone);
            l.hasLimit = r.u8() != 0;
            if (l.hasLimit) {
               r.vec(l.min, 3);
               r.vec(l.max, 3);
            }
            b.ikLinks.push_back(l);
         }
      }
   }
   if (!r.ok) return false;
   m_numBone = (unsigned short)bones.size();
   {
      int nb = (int)bones.size();
      std::vector<int> ikOf(bones.size(), -1);  /* IK bone whose chain includes this bone */
      std::vector<bool> isTarget(bones.size(), false);
      std::vector<bool> kneeLike(bones.size(), false);

      /* indices must point inside the bone list; anything else (including negatives other
         than -1) means none */
      for (i = 0; i < bones.size(); i++) {
         PMXBone &b = bones[i];
         if (b.parent < 0 || b.parent >= nb || b.parent == (int)i) b.parent = -1;
         if (b.tail < 0 || b.tail >= nb) b.tail = -1;
         if (b.inheritParent < 0 || b.inheritParent >= nb) b.inheritParent = -1;
         if (b.ikTarget < 0 || b.ikTarget >= nb) b.ikTarget = -1;
         for (j = 0; j < b.ikLinks.size(); j++)
            if (b.ikLinks[j].bone < 0 || b.ikLinks[j].bone >= nb) b.ikLinks[j].bone = -1;
      }
      /* a parent cycle would make the bone ordering below loop forever */
      for (i = 0; i < bones.size(); i++) {
         int steps = 0;
         for (int q = bones[i].parent; q >= 0; q = bones[q].parent)
            if (++steps > nb)
               return false;
      }
      /* vertex bone indices past the bone list point at bone 0 */
      for (i = 0; i < m_numVertex; i++) {
         if (m_bone1List[i] >= nb) m_bone1List[i] = 0;
         if (m_bone2List[i] >= nb) m_bone2List[i] = 0;
#ifdef MY_EXTRADEFORMATION
         if (m_bone3List[i] >= nb) m_bone3List[i] = 0;
         if (m_bone4List[i] >= nb) m_bone4List[i] = 0;
#endif /* MY_EXTRADEFORMATION */
      }
      if (nb == 0 && m_numVertex > 0)
         return false;
      /* bone types as PMXEditor's PMD export sets them */
      for (i = 0; i < bones.size(); i++) {
         if (bones[i].ikTarget < 0) continue;
         isTarget[bones[i].ikTarget] = true;
         for (j = 0; j < bones[i].ikLinks.size(); j++) {
            const PMXIKLink &l = bones[i].ikLinks[j];
            if (l.bone < 0) continue;
            if (ikOf[l.bone] < 0) ikOf[l.bone] = (int)i;
            /* a link that may only bend backwards about X is a knee, whatever its name */
            if (l.hasLimit && l.min[1] == 0.0f && l.max[1] == 0.0f && l.min[2] == 0.0f && l.max[2] == 0.0f && l.max[0] <= 0.0f && l.min[0] < 0.0f)
               kneeLike[l.bone] = true;
         }
      }

      m_boneList = (PMDBone *)MMDFiles_alignedmalloc(sizeof(PMDBone) * m_numBone, 16);
      for (i = 0; i < bones.size(); i++) {
         PMXBone &b = bones[i];
         PMDFile_Bone fb;
         new (m_boneList + i) PMDBone();
         memset(&fb, 0, sizeof(PMDFile_Bone));
         fb.parentBoneID = (short)b.parent;
         fb.childBoneID = (short)b.tail;
         fb.targetBoneID = 0;
         if (b.flags & PMX_BONE_IK) {
            fb.type = IK_DESTINATION;
         } else if (ikOf[i] >= 0) {
            fb.type = UNDER_IK;
            fb.targetBoneID = (short)ikOf[i];
         } else if (isTarget[i]) {
            fb.type = IK_TARGET;
         } else if (b.flags & PMX_BONE_FIXED_AXIS) {
            fb.type = TWIST;
         } else if (!(b.flags & PMX_BONE_VISIBLE)) {
            fb.type = NO_DISP;
         } else if (b.flags & PMX_BONE_MOVE) {
            fb.type = ROTATE_AND_MOVE;
         } else {
            fb.type = ROTATE;
         }
         for (j = 0; j < 3; j++)
            fb.pos[j] = b.pos[j];
         if (!m_boneList[i].setup(&fb, m_boneList, m_numBone, &m_rootBone))
            return false;
         m_boneList[i].setId(i);
         m_boneList[i].setName(b.name.c_str());
         if (strstr(b.name.c_str(), PMDBONE_KNEENAME) || kneeLike[i])
            m_boneList[i].setLimitAngleX(true);
         if (MMDFiles_strequal(m_boneList[i].getName(), PMDMODEL_CENTERBONENAME) == true && m_centerBone == NULL)
            m_centerBone = &(m_boneList[i]);
      }
      if (!m_centerBone && m_numBone >= 1)
         m_centerBone = &(m_boneList[0]);

      /* extended bone parameters, as parseExtCsv applies them */
      for (i = 0; i < bones.size(); i++) {
         PMXBone &b = bones[i];
         if (b.flags & PMX_BONE_AFTER_PHYSICS)
            m_boneList[i].enableProcessingAfterPhysics();
         else
            m_boneList[i].disableProcessingAfterPhysics();
         m_boneList[i].setProcessLayer((short)b.layer);
         if (b.inheritParent >= 0) {
            if (b.flags & PMX_BONE_INHERIT_ROT)
               m_boneList[i].setFollowRotate(b.inheritRate, &(m_boneList[b.inheritParent]));
            if (b.flags & PMX_BONE_INHERIT_MOVE)
               m_boneList[i].setFollowMove(b.inheritRate, &(m_boneList[b.inheritParent]));
         }
      }
      m_hasExtBoneParam = true;

      /* ordered bone list: roots first, then every bone after its parent */
      if (m_numBone > 0) {
         unsigned short k = 0, l;
         PMDBone *bMatch;
         unsigned short jj;
         int moved;
         m_orderedBoneList = (PMDBone **)malloc(sizeof(PMDBone *) * m_numBone);
         for (jj = 0; jj < m_numBone; jj++)
            if (bones[jj].parent == -1)
               m_orderedBoneList[k++] = &(m_boneList[jj]);
         l = k;
         for (jj = 0; jj < m_numBone; jj++)
            if (bones[jj].parent != -1)
               m_orderedBoneList[l++] = &(m_boneList[jj]);
         do {
            moved = 0;
            for (jj = k; jj < m_numBone; jj++) {
               for (l = 0; l < jj; l++)
                  if (m_orderedBoneList[l] == m_orderedBoneList[jj]->getParentBone())
                     break;
               if (l >= jj) {
                  bMatch = m_orderedBoneList[jj];
                  if (jj < m_numBone - 1)
                     memmove(&m_orderedBoneList[jj], &m_orderedBoneList[jj + 1], sizeof(PMDBone *) * (m_numBone - 1 - jj));
                  m_orderedBoneList[m_numBone - 1] = bMatch;
                  moved = 1;
               }
            }
         } while (moved != 0);
      }
      for (i = 0; i < m_numBone; i++)
         m_boneList[i].computeOffset();

      /* IK chains */
      m_numIK = 0;
      for (i = 0; i < bones.size(); i++)
         if ((bones[i].flags & PMX_BONE_IK) && bones[i].ikTarget >= 0)
            m_numIK++;
      if (m_numIK > 0) {
         unsigned short n = 0;
         m_IKList = new PMDIK[m_numIK];
         for (i = 0; i < bones.size(); i++) {
            PMXBone &b = bones[i];
            PMDFile_IK fik;
            std::vector<short> links;
            if (!(b.flags & PMX_BONE_IK) || b.ikTarget < 0) continue;
            for (j = 0; j < b.ikLinks.size() && links.size() < 255; j++)
               if (b.ikLinks[j].bone >= 0)
                  links.push_back((short)b.ikLinks[j].bone);
            fik.destBoneID = (short)i;
            fik.targetBoneID = (short)b.ikTarget;
            fik.numLink = (unsigned char)links.size();
            fik.numIteration = (unsigned short)(b.ikLoops < 0 ? 0 : (b.ikLoops > 65535 ? 65535 : b.ikLoops));
            fik.angleConstraint = b.ikLimit / 4.0f; /* PMD stores a quarter of the per-step limit */
            m_IKList[n++].setup(&fik, links.empty() ? NULL : (const unsigned char *)&links[0], m_boneList);
         }
      }
   }

   /* morphs */
   {
      int numMorph = r.count(14);
      std::vector<std::string> morphNames;
      struct GroupEntry { unsigned short morph; int target; float rate; };
      std::vector<GroupEntry> groupEntries;
      unsigned long nGroup = 0, nVertex = 0, nBone = 0, nUV = 0, nMaterial = 0;
      const unsigned char *morphStart;

      if (!r.ok) return false;
      /* first pass: count morphs by type to allocate the lists */
      morphStart = r.p;
      for (int k = 0; k < numMorph && r.ok; k++) {
         unsigned char type;
         int n;
         morphNames.push_back(r.text());
         r.text(); /* English name */
         r.u8(); /* panel */
         type = r.u8();
         n = r.count(1);
         switch (type) {
         case 0: nGroup++; r.skip((size_t)n * (sizeMorph + 4)); break;
         case 1: nVertex++; r.skip((size_t)n * (sizeVertex + 12)); break;
         case 2: nBone++; r.skip((size_t)n * (sizeBone + 28)); break;
         case 3: nUV++; r.skip((size_t)n * (sizeVertex + 16)); break;
         case 4: case 5: case 6: case 7: r.skip((size_t)n * (sizeVertex + 16)); break; /* additional UV: unused */
         case 8: nMaterial++; r.skip((size_t)n * (sizeMaterial + 1 + 4 * 28)); break;
         case 9: r.skip((size_t)n * (sizeMorph + 4)); break; /* flip */
         case 10: r.skip((size_t)n * (sizeRigid + 1 + 24)); break; /* impulse */
         default: r.ok = false; break;
         }
      }
      if (!r.ok) return false;
      if (nGroup) m_groupMorphList = new PMDGroupMorph[nGroup];
      if (nVertex) m_vertexMorphList = new PMDVertexMorph[nVertex];
      if (nBone) m_boneMorphList = new PMDBoneMorph[nBone];
      if (nUV) m_uvMorphList = new PMDUVMorph[nUV];
      if (nMaterial) m_materialMorphList = new PMDMaterialMorph[nMaterial];
      m_numGroupMorph = m_numVertexMorph = m_numBoneMorph = m_numUVMorph = m_numMaterialMorph = 0;

      /* second pass: fill them */
      r.p = morphStart;
      for (int k = 0; k < numMorph && r.ok; k++) {
         unsigned char type;
         int n;
         std::string name = r.text();
         r.text();
         r.u8();
         type = r.u8();
         n = r.count(1);
         if (!r.ok) break;
         /* the second pass must see the morphs the first one counted */
         if ((type == 0 && m_numGroupMorph >= nGroup) || (type == 1 && m_numVertexMorph >= nVertex) || (type == 2 && m_numBoneMorph >= nBone)
             || (type == 3 && m_numUVMorph >= nUV) || (type == 8 && m_numMaterialMorph >= nMaterial) || type > 10)
            return false;
         if (type == 0) {
            PMDGroupMorph *m = &(m_groupMorphList[m_numGroupMorph++]);
            m->setup(name.c_str());
            for (int e = 0; e < n && r.ok; e++) {
               int target = r.idx(sizeMorph);
               float rate = r.f32();
               if (target >= 0 && target < numMorph)
                  m->add(morphNames[target].c_str(), rate);
            }
         } else if (type == 1) {
            PMDVertexMorph *m = &(m_vertexMorphList[m_numVertexMorph++]);
            m->setup(name.c_str());
            for (int e = 0; e < n && r.ok; e++) {
               int v = r.vidx(sizeVertex);
               float f[3];
               btVector3 pos;
               r.vec(f, 3);
#ifdef MMDFILES_CONVERTCOORDINATESYSTEM
               pos.setValue(f[0], f[1], -f[2]);
#else
               pos.setValue(f[0], f[1], f[2]);
#endif /* MMDFILES_CONVERTCOORDINATESYSTEM */
               if (v >= 0 && (unsigned int)v < m_numVertex)
                  m->add(v, &pos);
            }
         } else if (type == 2) {
            PMDBoneMorph *m = &(m_boneMorphList[m_numBoneMorph++]);
            m->setup(name.c_str());
            for (int e = 0; e < n && r.ok; e++) {
               int b = r.idx(sizeBone);
               float t[3], q[4];
               btVector3 pos;
               btQuaternion rot;
               r.vec(t, 3);
               r.vec(q, 4);
#ifdef MMDFILES_CONVERTCOORDINATESYSTEM
               pos.setValue(t[0], t[1], -t[2]);
               rot.setValue(-q[0], -q[1], q[2], q[3]);
#else
               pos.setValue(t[0], t[1], t[2]);
               rot.setValue(q[0], q[1], q[2], q[3]);
#endif /* MMDFILES_CONVERTCOORDINATESYSTEM */
               if (b >= 0 && b < m_numBone)
                  m->add(&(m_boneList[b]), &pos, &rot);
            }
         } else if (type == 3) {
            PMDUVMorph *m = &(m_uvMorphList[m_numUVMorph++]);
            m->setup(name.c_str());
            for (int e = 0; e < n && r.ok; e++) {
               int v = r.vidx(sizeVertex);
               float f[4];
               TexCoord tex;
               r.vec(f, 4);
               tex.u = f[0];
               tex.v = f[1];
               if (v >= 0 && (unsigned int)v < m_numVertex)
                  m->add(v, &tex);
            }
         } else if (type == 8) {
            PMDMaterialMorph *m = &(m_materialMorphList[m_numMaterialMorph++]);
            m->setup(name.c_str());
            for (int e = 0; e < n && r.ok; e++) {
               PMDMaterialMorphElem el;
               int mat = r.idx(sizeMaterial);
               memset(&el, 0, sizeof(PMDMaterialMorphElem));
               el.midx = (mat >= 0 && (unsigned int)mat < m_numMaterial) ? mat : -1;
               el.addflag = r.u8() == 1 ? true : false;
               r.vec(el.diffuse, 4);
               r.vec(el.specular, 3);
               el.shiness = r.f32();
               r.vec(el.ambient, 3);
               r.vec(el.edgecol, 4);
               el.edgesize = r.f32();
               r.vec(el.tex, 4);
               r.vec(el.sphere, 4);
               r.vec(el.toon, 4);
               m->add(&el);
            }
         } else {
            /* additional UV, flip, impulse: skipped (counted above) */
            switch (type) {
            case 4: case 5: case 6: case 7: r.skip((size_t)n * (sizeVertex + 16)); break;
            case 9: r.skip((size_t)n * (sizeMorph + 4)); break;
            case 10: r.skip((size_t)n * (sizeRigid + 1 + 24)); break;
            }
         }
      }
      if (!r.ok) return false;
   }

   /* display frames (skip) */
   {
      int n = r.count(9);
      for (int k = 0; k < n && r.ok; k++) {
         int m;
         r.text();
         r.text();
         r.u8();
         m = r.count(2);
         for (int e = 0; e < m && r.ok; e++) {
            unsigned char t = r.u8();
            r.idx(t ? sizeMorph : sizeBone);
         }
      }
   }
   if (!r.ok) return false;

   /* rigid bodies and joints */
   m_numRigidBody = 0;
   m_numConstraint = 0;
   if (r.p < r.end && m_bulletPhysics) {
      btVector3 tmpVector;
      int n;

      /* get model offset, and update bone matrix to apply root bone offset to bone position */
      m_rootBone.getOffset(&tmpVector);
      for (i = 0; i < m_numBone; i++)
         m_orderedBoneList[i]->update();

      n = r.count(70);
      if (!r.ok) return false;
      if (n > 0 && m_numBone == 0)
         return false;
      if (n > 0) {
         m_numRigidBody = (unsigned int)n;
         m_rigidBodyList = (PMDRigidBody *)MMDFiles_alignedmalloc(sizeof(PMDRigidBody) * m_numRigidBody, 16);
         for (i = 0; i < m_numRigidBody; i++)
            new (m_rigidBodyList + i) PMDRigidBody();
         for (i = 0; i < m_numRigidBody && r.ok; i++) {
            PMDFile_RigidBody fr;
            int bone;
            float size3[3], pos[3], rot[3];
            PMDBone *b;
            memset(&fr, 0, sizeof(PMDFile_RigidBody));
            r.text();
            r.text();
            bone = r.idx(sizeBone);
            fr.collisionGroupID = r.u8();
            fr.collisionMask = r.u16();
            fr.shapeType = r.u8();
            r.vec(size3, 3);
            r.vec(pos, 3);
            r.vec(rot, 3);
            fr.mass = r.f32();
            fr.linearDamping = r.f32();
            fr.angularDamping = r.f32();
            fr.restitution = r.f32();
            fr.friction = r.f32();
            fr.type = r.u8();
            if (!r.ok) break;
            fr.width = size3[0];
            fr.height = size3[1];
            fr.depth = size3[2];
            if (bone < 0 || bone >= m_numBone) {
               fr.boneID = 0xFFFF;
               b = &(m_boneList[0]);
            } else {
               fr.boneID = (unsigned short)bone;
               b = &(m_boneList[bone]);
            }
            /* PMX positions are absolute, PMD ones relative to the bone */
            fr.pos[0] = pos[0] - bones[b->getId()].pos[0];
            fr.pos[1] = pos[1] - bones[b->getId()].pos[1];
            fr.pos[2] = pos[2] - bones[b->getId()].pos[2];
            for (j = 0; j < 3; j++)
               fr.rot[j] = rot[j];
            if (!m_rigidBodyList[i].setup(&fr, b))
               return false;
            /* flag the bones under simulation in order to skip IK solving for those bones */
            if (fr.type != 0 && fr.boneID != 0xFFFF)
               m_boneList[fr.boneID].setSimulatedFlag(true);
         }
         if (!r.ok) return false;
      }

      if (r.p < r.end) {
         n = r.count(107);
         if (!r.ok) return false;
         if (n > 0) {
            m_numConstraint = (unsigned int)n;
            m_constraintList = new PMDConstraint[m_numConstraint];
            for (i = 0; i < m_numConstraint && r.ok; i++) {
               PMDFile_Constraint fc;
               int a, b;
               memset(&fc, 0, sizeof(PMDFile_Constraint));
               r.text();
               r.text();
               r.u8(); /* joint type: all loaded as spring 6DOF */
               a = r.idx(sizeRigid);
               b = r.idx(sizeRigid);
               r.vec(fc.pos, 3);
               r.vec(fc.rot, 3);
               r.vec(fc.limitPosFrom, 3);
               r.vec(fc.limitPosTo, 3);
               r.vec(fc.limitRotFrom, 3);
               r.vec(fc.limitRotTo, 3);
               r.vec(fc.stiffness, 6);
               if (!r.ok) break;
               if (a < 0 || b < 0 || (unsigned int)a >= m_numRigidBody || (unsigned int)b >= m_numRigidBody)
                  return false;
               fc.bodyIDA = (unsigned int)a;
               fc.bodyIDB = (unsigned int)b;
               if (!m_constraintList[i].setup(&fc, m_rigidBodyList, &tmpVector))
                  return false;
            }
            if (!r.ok) return false;
         }
      }
      /* soft bodies (PMX 2.1) are not supported */
   }

   /* name indices for fast lookup, morph serialization and group links, as parseExtCsv does */
   for (j = 0; j < m_numBone; j++) {
      char *name = m_boneList[j].getName();
      if (name) m_name2bone.add(name, strlen(name), &(m_boneList[j]));
   }
   for (j = 0; j < m_numBoneMorph; j++) {
      char *name = m_boneMorphList[j].getName();
      if (name) m_name2bonemorph.add(name, strlen(name), &(m_boneMorphList[j]));
   }
   for (j = 0; j < m_numVertexMorph; j++) {
      char *name = m_vertexMorphList[j].getName();
      if (name) m_name2vertexmorph.add(name, strlen(name), &(m_vertexMorphList[j]));
   }
   for (j = 0; j < m_numUVMorph; j++) {
      char *name = m_uvMorphList[j].getName();
      if (name) m_name2uvmorph.add(name, strlen(name), &(m_uvMorphList[j]));
   }
   for (j = 0; j < m_numMaterialMorph; j++) {
      char *name = m_materialMorphList[j].getName();
      if (name) m_name2materialmorph.add(name, strlen(name), &(m_materialMorphList[j]));
   }
   for (j = 0; j < m_numGroupMorph; j++) {
      char *name = m_groupMorphList[j].getName();
      if (name) m_name2groupmorph.add(name, strlen(name), &(m_groupMorphList[j]));
   }
   for (j = 0; j < m_numVertexMorph; j++)
      m_vertexMorphList[j].serialize();
   for (j = 0; j < m_numUVMorph; j++)
      m_uvMorphList[j].serialize();
   for (j = 0; j < m_numGroupMorph; j++) {
      PMDGroupMorphElem *p;
      m_groupMorphList[j].setHasBoneFlag(false);
      m_groupMorphList[j].setHasUVFlag(false);
      for (p = m_groupMorphList[j].getList(); p; p = p->next) {
         p->b = getBoneMorph(p->name);
         p->v = getVertexMorph(p->name);
         p->u = getUVMorph(p->name);
         p->m = getMaterialMorph(p->name);
         if (p->b) m_groupMorphList[j].setHasBoneFlag(true);
         if (p->u) m_groupMorphList[j].setHasUVFlag(true);
      }
   }

   m_hasExtParam = true;
   return true;
}
