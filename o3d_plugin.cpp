#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <map>

#define _DEBUG_LOG 1

static HMODULE g_hModule = NULL;
static char g_logPath[MAX_PATH] = {0};

static const char* GetLogFilePath() {
    if (g_logPath[0] != '\0') {
        return g_logPath;
    }
    char modPath[MAX_PATH] = {0};
    if (GetModuleFileNameA(g_hModule, modPath, MAX_PATH) > 0) {
        char* pSlash = strrchr(modPath, '\\');
        if (!pSlash) pSlash = strrchr(modPath, '/');
        if (pSlash) {
            *(pSlash + 1) = '\0';
            snprintf(g_logPath, sizeof(g_logPath), "%so3d_plugin.log", modPath);
            return g_logPath;
        }
    }
    return "o3d_plugin.log";
}

// Log helper for debugging
static void PluginLog(const char* fmt, ...) {
#ifdef _DEBUG_LOG
    FILE* f = fopen(GetLogFilePath(), "a");
    if (!f) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fflush(f);
    fclose(f);
#endif
}

// -------------------------------------------------------------
// OMSI O3D Structures and Constants
// -------------------------------------------------------------

#pragma pack(push, 1)

struct Vector2D {
    float x;
    float y;
};

struct Vector3D {
    float x;
    float y;
    float z;
};

struct Vector4D {
    float x;
    float y;
    float z;
    float w;
};

struct O3D_Vertex {
    Vector3D position;
    Vector3D normal;
    Vector2D uv;
};

struct O3D_Face {
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint16_t material_id;
};

struct O3D_Material {
    float faceColor[4];     // RGBA
    float specularColor[3]; // RGB
    float emissiveColor[3]; // RGB
    float power;
    std::string texFileName;
};

struct O3D_BoneWeight {
    uint16_t index;
    float weight;
};

struct O3D_Bone {
    std::string name;
    std::vector<O3D_BoneWeight> weights;
};

struct O3D_Model {
    uint8_t version;
    uint8_t flags;
    bool has_long_tris;
    bool has_encryption;
    uint32_t product_id;
    bool is_locked;

    std::vector<O3D_Vertex> vertices;
    std::vector<O3D_Face> faces;
    std::vector<O3D_Material> materials;
    float matrix[16];
    bool has_matrix;
    std::vector<O3D_Bone> bones;

    O3D_Model() : version(7), flags(0), has_long_tris(false), has_encryption(false),
                  product_id(0xFFFFFFFF), is_locked(false), has_matrix(false) {
        memset(matrix, 0, sizeof(matrix));
        matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0f; // Identity
    }
};

#pragma pack(pop)

enum O3D_DATA_TYPES : uint8_t {
    O3D_SEC_VERTEX    = 0x17,
    O3D_SEC_MATERIALS = 0x26,
    O3D_SEC_TRIS      = 0x49, // 'I'
    O3D_SEC_BONES     = 0x54, // 'T'
    O3D_SEC_TRANSFORM = 0x79  // 'y'
};



// -------------------------------------------------------------
// O3D Binary File Reader
// -------------------------------------------------------------

static bool ParseO3DData(const uint8_t* data, size_t fileSize, O3D_Model& model) {
    if (fileSize < 4) return false;
    size_t pos = 0;

    uint16_t header = *(const uint16_t*)(data + pos);
    pos += 2;
    if (header != 0x1984) {
        PluginLog("ParseO3DData: Invalid magic header: 0x%04x\n", header);
        return false;
    }

    model.version = data[pos++];
    if (model.version > 7) {
        PluginLog("ParseO3DData: Unsupported version: %u\n", model.version);
        return false;
    }

    if (model.version >= 4) {
        if (pos + 5 > fileSize) return false;
        model.flags = data[pos++];
        model.has_long_tris = (model.flags & 1) != 0;
        model.has_encryption = (model.flags & 2) != 0;
        model.product_id = *(const uint32_t*)(data + pos);
        pos += 4;
        model.is_locked = (model.product_id != 0xFFFFFFFF);
    } else {
        model.flags = 0;
        model.has_long_tris = false;
        model.has_encryption = false;
        model.product_id = 0xFFFFFFFF;
        model.is_locked = false;
    }

    while (pos < fileSize) {
        uint8_t sec_type = data[pos++];

        switch (sec_type) {
            case O3D_SEC_VERTEX: {
                uint32_t vert_count = 0;
                if (model.version >= 3) {
                    if (pos + 4 > fileSize) return false;
                    vert_count = *(const uint32_t*)(data + pos);
                    pos += 4;
                } else {
                    if (pos + 2 > fileSize) return false;
                    vert_count = *(const uint16_t*)(data + pos);
                    pos += 2;
                }

                if (pos + vert_count * sizeof(O3D_Vertex) > fileSize) return false;
                model.vertices.resize(vert_count);
                memcpy(model.vertices.data(), data + pos, vert_count * sizeof(O3D_Vertex));
                pos += vert_count * sizeof(O3D_Vertex);
                break;
            }

            case O3D_SEC_TRIS: {
                uint32_t face_count = 0;
                if (model.version >= 3) {
                    if (pos + 4 > fileSize) return false;
                    face_count = *(const uint32_t*)(data + pos);
                    pos += 4;
                } else {
                    if (pos + 2 > fileSize) return false;
                    face_count = *(const uint16_t*)(data + pos);
                    pos += 2;
                }

                model.faces.resize(face_count);
                if (model.has_long_tris) {
                    if (pos + face_count * 14 > fileSize) return false;
                    for (uint32_t i = 0; i < face_count; i++) {
                        model.faces[i].a = *(const uint32_t*)(data + pos); pos += 4;
                        model.faces[i].b = *(const uint32_t*)(data + pos); pos += 4;
                        model.faces[i].c = *(const uint32_t*)(data + pos); pos += 4;
                        model.faces[i].material_id = *(const uint16_t*)(data + pos); pos += 2;
                    }
                } else {
                    if (pos + face_count * 8 > fileSize) return false;
                    for (uint32_t i = 0; i < face_count; i++) {
                        model.faces[i].a = *(const uint16_t*)(data + pos); pos += 2;
                        model.faces[i].b = *(const uint16_t*)(data + pos); pos += 2;
                        model.faces[i].c = *(const uint16_t*)(data + pos); pos += 2;
                        model.faces[i].material_id = *(const uint16_t*)(data + pos); pos += 2;
                    }
                }
                break;
            }

            case O3D_SEC_MATERIALS: {
                if (pos + 2 > fileSize) return false;
                uint16_t mat_count = *(const uint16_t*)(data + pos);
                pos += 2;
                model.materials.resize(mat_count);

                for (uint16_t i = 0; i < mat_count; i++) {
                    if (pos + 44 > fileSize) return false;
                    memcpy(model.materials[i].faceColor, data + pos, 16); pos += 16;
                    memcpy(model.materials[i].specularColor, data + pos, 12); pos += 12;
                    memcpy(model.materials[i].emissiveColor, data + pos, 12); pos += 12;
                    memcpy(&model.materials[i].power, data + pos, 4); pos += 4;

                    if (pos >= fileSize) return false;
                    uint8_t tex_len = data[pos++];
                    if (tex_len > 0) {
                        if (pos + tex_len > fileSize) return false;
                        model.materials[i].texFileName.assign((const char*)(data + pos), tex_len);
                        pos += tex_len;
                    } else {
                        model.materials[i].texFileName.clear();
                    }
                }
                break;
            }

            case O3D_SEC_TRANSFORM: {
                if (pos + 64 > fileSize) return false;
                memcpy(model.matrix, data + pos, 64);
                pos += 64;
                model.has_matrix = true;
                break;
            }

            case O3D_SEC_BONES: {
                if (pos + 2 > fileSize) return false;
                uint16_t bone_count = *(const uint16_t*)(data + pos);
                pos += 2;
                model.bones.resize(bone_count);

                for (uint16_t i = 0; i < bone_count; i++) {
                    if (pos >= fileSize) return false;
                    uint8_t name_len = data[pos++];
                    if (name_len > 0) {
                        if (pos + name_len > fileSize) return false;
                        model.bones[i].name.assign((const char*)(data + pos), name_len);
                        pos += name_len;
                    }
                    if (pos + 2 > fileSize) return false;
                    uint16_t weight_count = *(const uint16_t*)(data + pos);
                    pos += 2;
                    if (pos + weight_count * 6 > fileSize) return false;
                    model.bones[i].weights.resize(weight_count);
                    for (uint16_t w = 0; w < weight_count; w++) {
                        model.bones[i].weights[w].index = *(const uint16_t*)(data + pos); pos += 2;
                        model.bones[i].weights[w].weight = *(const float*)(data + pos); pos += 4;
                    }
                }
                break;
            }

            default:
                PluginLog("ParseO3DData: Unknown section 0x%02X at offset %u\n", sec_type, (unsigned int)(pos - 1));
                return false;
        }
    }

    if (model.is_locked) {
        PluginLog("ParseO3DData: Model is locked/encrypted\n");
    }

    return true;
}

static bool ReadO3DFile(const wchar_t* filePath, O3D_Model& model) {
    FILE* f = _wfopen(filePath, L"rb");
    if (!f) {
        PluginLog("ReadO3DFile: Cannot open file: %ls\n", filePath);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fclose(f);
        return false;
    }

    std::vector<uint8_t> buffer(size);
    size_t readBytes = fread(buffer.data(), 1, size, f);
    fclose(f);

    if (readBytes != (size_t)size) return false;
    return ParseO3DData(buffer.data(), buffer.size(), model);
}

// -------------------------------------------------------------
// O3D Binary File Writer / Serializer
// -------------------------------------------------------------

static bool SerializeO3D(const O3D_Model& model, std::vector<uint8_t>& buf) {
    buf.clear();

    // Magic Header: 0x1984
    uint16_t magic = 0x1984;
    buf.insert(buf.end(), (const uint8_t*)&magic, (const uint8_t*)&magic + 2);

    bool long_tris = false;
    for (const auto& face : model.faces) {
        if (face.a > 0xFFFF || face.b > 0xFFFF || face.c > 0xFFFF) {
            long_tris = true;
            break;
        }
    }
    if (model.vertices.size() > 0xFFFF || model.faces.size() > 0xFFFF) {
        long_tris = true;
    }

    // Version: 1 by default (or 7 if mesh requires 32-bit indices)
    uint8_t ver = model.version;
    if (long_tris && ver < 4) {
        ver = 7;
    }
    buf.push_back(ver);

    if (ver >= 4) {
        // Flags byte: Bit 0 = Long Tris, Bit 1 = Encrypted
        uint8_t flags = 0;
        if (long_tris) flags |= 1;
        buf.push_back(flags);

        // Product ID: 0xFFFFFFFF for clean OMSI 2 files
        uint32_t pid = 0xFFFFFFFF;
        buf.insert(buf.end(), (const uint8_t*)&pid, (const uint8_t*)&pid + 4);
    }

    // Section 0x17: Vertices
    uint8_t sec_vert = O3D_SEC_VERTEX;
    buf.push_back(sec_vert);
    uint32_t num_verts = (uint32_t)model.vertices.size();
    if (ver >= 3) {
        buf.insert(buf.end(), (const uint8_t*)&num_verts, (const uint8_t*)&num_verts + 4);
    } else {
        uint16_t num_verts16 = (uint16_t)num_verts;
        buf.insert(buf.end(), (const uint8_t*)&num_verts16, (const uint8_t*)&num_verts16 + 2);
    }

    if (num_verts > 0) {
        const uint8_t* pVData = (const uint8_t*)model.vertices.data();
        buf.insert(buf.end(), pVData, pVData + num_verts * sizeof(O3D_Vertex));
    }

    // Section 0x49: Triangles
    uint8_t sec_tris = O3D_SEC_TRIS;
    buf.push_back(sec_tris);
    uint32_t num_tris = (uint32_t)model.faces.size();
    if (ver >= 3) {
        buf.insert(buf.end(), (const uint8_t*)&num_tris, (const uint8_t*)&num_tris + 4);
    } else {
        uint16_t num_tris16 = (uint16_t)num_tris;
        buf.insert(buf.end(), (const uint8_t*)&num_tris16, (const uint8_t*)&num_tris16 + 2);
    }

    if (ver >= 4 && long_tris) {
        for (const auto& face : model.faces) {
            buf.insert(buf.end(), (const uint8_t*)&face.a, (const uint8_t*)&face.a + 4);
            buf.insert(buf.end(), (const uint8_t*)&face.b, (const uint8_t*)&face.b + 4);
            buf.insert(buf.end(), (const uint8_t*)&face.c, (const uint8_t*)&face.c + 4);
            buf.insert(buf.end(), (const uint8_t*)&face.material_id, (const uint8_t*)&face.material_id + 2);
        }
    } else {
        for (const auto& face : model.faces) {
            uint16_t a = (uint16_t)face.a;
            uint16_t b = (uint16_t)face.b;
            uint16_t c = (uint16_t)face.c;
            uint16_t mid = face.material_id;
            buf.insert(buf.end(), (const uint8_t*)&a, (const uint8_t*)&a + 2);
            buf.insert(buf.end(), (const uint8_t*)&b, (const uint8_t*)&b + 2);
            buf.insert(buf.end(), (const uint8_t*)&c, (const uint8_t*)&c + 2);
            buf.insert(buf.end(), (const uint8_t*)&mid, (const uint8_t*)&mid + 2);
        }
    }

    // Section 0x26: Materials
    uint8_t sec_mat = O3D_SEC_MATERIALS;
    buf.push_back(sec_mat);
    uint16_t num_mats = (uint16_t)model.materials.size();
    buf.insert(buf.end(), (const uint8_t*)&num_mats, (const uint8_t*)&num_mats + 2);

    for (const auto& mat : model.materials) {
        buf.insert(buf.end(), (const uint8_t*)mat.faceColor, (const uint8_t*)mat.faceColor + 16);
        buf.insert(buf.end(), (const uint8_t*)mat.specularColor, (const uint8_t*)mat.specularColor + 12);
        buf.insert(buf.end(), (const uint8_t*)mat.emissiveColor, (const uint8_t*)mat.emissiveColor + 12);
        buf.insert(buf.end(), (const uint8_t*)&mat.power, (const uint8_t*)&mat.power + 4);
        uint8_t tlen = (uint8_t)mat.texFileName.length();
        buf.push_back(tlen);
        if (tlen > 0) {
            buf.insert(buf.end(), mat.texFileName.c_str(), mat.texFileName.c_str() + tlen);
        }
    }

    // Section 0x79: Transform Matrix
    uint8_t sec_matrix = O3D_SEC_TRANSFORM;
    buf.push_back(sec_matrix);
    buf.insert(buf.end(), (const uint8_t*)model.matrix, (const uint8_t*)model.matrix + 64);

    // Section 0x54: Bones (if present)
    if (!model.bones.empty()) {
        uint8_t sec_bones = O3D_SEC_BONES;
        buf.push_back(sec_bones);
        uint16_t num_bones = (uint16_t)model.bones.size();
        buf.insert(buf.end(), (const uint8_t*)&num_bones, (const uint8_t*)&num_bones + 2);
        for (const auto& bone : model.bones) {
            uint8_t blen = (uint8_t)bone.name.length();
            buf.push_back(blen);
            if (blen > 0) buf.insert(buf.end(), bone.name.c_str(), bone.name.c_str() + blen);
            uint16_t wcnt = (uint16_t)bone.weights.size();
            buf.insert(buf.end(), (const uint8_t*)&wcnt, (const uint8_t*)&wcnt + 2);
            for (const auto& w : bone.weights) {
                buf.insert(buf.end(), (const uint8_t*)&w.index, (const uint8_t*)&w.index + 2);
                buf.insert(buf.end(), (const uint8_t*)&w.weight, (const uint8_t*)&w.weight + 4);
            }
        }
    }

    return true;
}

static bool WriteO3DFile(const wchar_t* filePath, const O3D_Model& model) {
    std::vector<uint8_t> buf;
    if (!SerializeO3D(model, buf)) return false;
    FILE* f = _wfopen(filePath, L"wb");
    if (!f) {
        PluginLog("WriteO3DFile: Cannot open destination file: %ls\n", filePath);
        return false;
    }
    fwrite(buf.data(), 1, buf.size(), f);
    fclose(f);
    return true;
}

// -------------------------------------------------------------
// Deep Exploration 5.7 SDK Virtual Tables & Structures
// -------------------------------------------------------------

#pragma pack(push, 1)

struct DE_POINT2D {
    float u;
    float v;
};

struct DE_POINT3D {
    float x;
    float y;
    float z;
};

struct DE_MATRIX3D {
    double m[4][4];
};

#pragma pack(pop)

// Forward declarations of host types
typedef void* P_e3_HOST;
typedef void* P_e3_STREAM;
typedef void* P_e3_SCENE;
typedef void* P_e3_NODE;
typedef void* P_e3_MESH;
typedef void* P_e3_MATERIAL;

// Unique GUID for O3D format in Deep Exploration
static const GUID O3D_TYPE_GUID = { 0x19840e3d, 0x4f4d, 0x5349, { 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 } };

// Format Registration structure expected by Deep Exploration (size 0x52 = 82 bytes)
#pragma pack(push, 1)
struct DE_FILETYPE_REG {
    DWORD size;          // 0x00: 0x52 (82 bytes)
    DWORD flags;         // 0x04: 0x23c01
    const wchar_t* ext;  // 0x08: L"o3d"
    const wchar_t* desc; // 0x0c: L"OMSI 2 3D Model (*.o3d)"
    GUID  guid1;         // 0x10: 16 bytes
    DWORD cookie;        // 0x20: cookie
    const char* optName; // 0x24: NULL
    void* callback;      // 0x28: callback
    GUID  e3_type;       // 0x2c: Non-zero GUID
    DWORD reserved[5];   // 0x3c: 20 bytes
    WORD  reserved_end;  // 0x50: 2 bytes
};
#pragma pack(pop)

// -------------------------------------------------------------
// 3D Matrix and Vector Mathematics
// -------------------------------------------------------------

static inline void Matrix4x4_Identity(DE_MATRIX3D* out) {
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            out->m[r][c] = (r == c) ? 1.0 : 0.0;
        }
    }
}

static inline bool Matrix4x4_IsIdentity(const DE_MATRIX3D* m) {
    if (!m) return true;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            double expVal = (r == c) ? 1.0 : 0.0;
            if (fabs(m->m[r][c] - expVal) > 1e-4) return false;
        }
    }
    return true;
}

// Convert Section 0x79 OMSI matrix to Deep Exploration basis: M_DE = S * M_OMSI * S
static inline void OmsiMatrixToDEMatrix(const float* omsiMat, DE_MATRIX3D* deMat) {
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int srcR = (r == 1) ? 2 : ((r == 2) ? 1 : r);
            int srcC = (c == 1) ? 2 : ((c == 2) ? 1 : c);
            deMat->m[r][c] = (double)omsiMat[srcR * 4 + srcC];
        }
    }
}

// Convert Deep Exploration matrix to OMSI Section 0x79: M_OMSI = S * M_DE * S
static inline void DEMatrixToOmsiMatrix(const DE_MATRIX3D* deMat, float* omsiMat) {
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int srcR = (r == 1) ? 2 : ((r == 2) ? 1 : r);
            int srcC = (c == 1) ? 2 : ((c == 2) ? 1 : c);
            omsiMat[r * 4 + c] = (float)deMat->m[srcR][srcC];
        }
    }
}

// Invert 4x4 affine row-vector matrix:
// [ R   0 ]
// [ T   1 ]
// Inverse:
// [ R^-1      0 ]
// [ -T*R^-1   1 ]
static bool Matrix4x4_InvertAffine(const DE_MATRIX3D* in, DE_MATRIX3D* out) {
    double R[3][3];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            R[i][j] = in->m[i][j];
        }
    }

    double det = R[0][0] * (R[1][1] * R[2][2] - R[1][2] * R[2][1])
               - R[0][1] * (R[1][0] * R[2][2] - R[1][2] * R[2][0])
               + R[0][2] * (R[1][0] * R[2][1] - R[1][1] * R[2][0]);

    if (fabs(det) < 1e-9) {
        Matrix4x4_Identity(out);
        return false;
    }

    double invdet = 1.0 / det;
    double invR[3][3];
    invR[0][0] =  (R[1][1] * R[2][2] - R[1][2] * R[2][1]) * invdet;
    invR[0][1] = -(R[0][1] * R[2][2] - R[0][2] * R[2][1]) * invdet;
    invR[0][2] =  (R[0][1] * R[1][2] - R[0][2] * R[1][1]) * invdet;

    invR[1][0] = -(R[1][0] * R[2][2] - R[1][2] * R[2][0]) * invdet;
    invR[1][1] =  (R[0][0] * R[2][2] - R[0][2] * R[2][0]) * invdet;
    invR[1][2] = -(R[0][0] * R[1][2] - R[0][2] * R[1][0]) * invdet;

    invR[2][0] =  (R[1][0] * R[2][1] - R[1][1] * R[2][0]) * invdet;
    invR[2][1] = -(R[0][0] * R[2][1] - R[0][1] * R[2][0]) * invdet;
    invR[2][2] =  (R[0][0] * R[1][1] - R[0][1] * R[1][0]) * invdet;

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            out->m[i][j] = invR[i][j];
        }
        out->m[i][3] = 0.0;
    }

    double Tx = in->m[3][0];
    double Ty = in->m[3][1];
    double Tz = in->m[3][2];

    out->m[3][0] = -(Tx * invR[0][0] + Ty * invR[1][0] + Tz * invR[2][0]);
    out->m[3][1] = -(Tx * invR[0][1] + Ty * invR[1][1] + Tz * invR[2][1]);
    out->m[3][2] = -(Tx * invR[0][2] + Ty * invR[1][2] + Tz * invR[2][2]);
    out->m[3][3] = 1.0;

    return true;
}

static inline void Matrix4x4_TransformPoint(const DE_MATRIX3D* m, double inX, double inY, double inZ, double& outX, double& outY, double& outZ) {
    outX = inX * m->m[0][0] + inY * m->m[1][0] + inZ * m->m[2][0] + m->m[3][0];
    outY = inX * m->m[0][1] + inY * m->m[1][1] + inZ * m->m[2][1] + m->m[3][1];
    outZ = inX * m->m[0][2] + inY * m->m[1][2] + inZ * m->m[2][2] + m->m[3][2];
}

static inline void Matrix4x4_TransformNormal(const DE_MATRIX3D* m, double inX, double inY, double inZ, double& outX, double& outY, double& outZ) {
    double tx = inX * m->m[0][0] + inY * m->m[1][0] + inZ * m->m[2][0];
    double ty = inX * m->m[0][1] + inY * m->m[1][1] + inZ * m->m[2][1];
    double tz = inX * m->m[0][2] + inY * m->m[1][2] + inZ * m->m[2][2];
    double len = sqrt(tx * tx + ty * ty + tz * tz);
    if (len > 1e-6) {
        outX = tx / len;
        outY = ty / len;
        outZ = tz / len;
    } else {
        outX = inX; outY = inY; outZ = inZ;
    }
}

// -------------------------------------------------------------
// Deep Exploration VTable Helpers
// -------------------------------------------------------------

static inline bool Stream_GetFileNameW(P_e3_STREAM stream, wchar_t* outBuf, int maxLen) {
    if (!stream) return false;
    void** vt = *(void***)stream;
    typedef bool (__stdcall *Func)(void*, wchar_t*, int, unsigned int);
    Func f = (Func)vt[37]; // vt[37] writes UTF-16 wchar_t
    return f(stream, outBuf, maxLen, 1);
}

static inline bool Stream_ReadData(P_e3_STREAM stream, void* buf, int bytes) {
    if (!stream) return false;
    void** vt = *(void***)stream;
    typedef bool (__stdcall *Func)(void*, void*, int, int);
    Func f = (Func)vt[50];
    return f(stream, buf, 1, bytes);
}

static inline unsigned int Stream_Write(P_e3_STREAM stream, const void* buf, unsigned int bytes) {
    if (!stream) return 0;
    void** vt = *(void***)stream;
    typedef unsigned int (__stdcall *Func)(void*, const void*, unsigned int);
    Func f = (Func)vt[19]; // vt[19] = 0x10017cb3: Write(void* stream, const void* buf, unsigned int size)
    return f(stream, buf, bytes);
}

static inline int Stream_GetSize(P_e3_STREAM stream) {
    if (!stream) return 0;
    void** vt = *(void***)stream;
    typedef int (__stdcall *Func)(void*);
    Func f = (Func)vt[16];
    return f(stream);
}

static inline bool Stream_Reset(P_e3_STREAM stream) {
    if (!stream) return false;
    void** vt = *(void***)stream;
    typedef bool (__stdcall *Func)(void*);
    Func f = (Func)vt[24];
    return f(stream);
}

static inline bool Stream_IsEOF(P_e3_STREAM stream) {
    if (!stream) return true;
    void** vt = *(void***)stream;
    typedef bool (__stdcall *Func)(void*);
    Func f = (Func)vt[25];
    return f(stream);
}

static inline void Node_SetNameA(P_e3_NODE node, const char* name) {
    if (!node) return;
    void** vt = *(void***)node;
    typedef bool (__stdcall *Func)(void*, const char*);
    Func f = (Func)vt[21];
    f(node, name);
}

static inline P_e3_NODE Scene_CreateMeshChild(P_e3_SCENE scene, const char* name) {
    if (!scene) return NULL;
    void** vt = *(void***)scene;
    typedef P_e3_NODE (__stdcall *Func)(void*, unsigned int, const char*, const char*);
    Func f = (Func)vt[46];
    P_e3_NODE node = f(scene, 0, NULL, "e3mesh");
    if (node && name) {
        Node_SetNameA(node, name);
    }
    return node;
}

static inline P_e3_MATERIAL Scene_CreateMaterial(P_e3_SCENE scene) {
    if (!scene) return NULL;
    void** vt = *(void***)scene;
    typedef P_e3_MATERIAL (__stdcall *Func)(void*, unsigned int);
    Func f = (Func)vt[81];
    return f(scene, 0);
}

// In e3_NODE vtable:
// vt[38] = 0x10093b7e: ?SetMatrix@e3_NODE@@UAG_NPBUMATRIX3D@@_N@Z
static inline bool Node_SetMatrix(P_e3_NODE node, const DE_MATRIX3D* mat, bool isPivot = false) {
    if (!node) return false;
    void** vt = *(void***)node;
    typedef bool (__stdcall *Func)(void*, const void*, bool);
    Func f = (Func)vt[38];
    return f(node, mat, isPivot);
}

// In e3_NODE vtable:
// vt[39] = 0x1009412c: ?GetMatrix@e3_NODE@@UAG_NPAUMATRIX3D@@_N@Z
static inline bool Node_GetMatrix(P_e3_NODE node, DE_MATRIX3D* mat, bool isPivot = false) {
    if (!node) return false;
    void** vt = *(void***)node;
    typedef bool (__stdcall *Func)(void*, void*, bool);
    Func f = (Func)vt[39];
    return f(node, mat, isPivot);
}

// In e3_NODE vtable:
// vt[40] = 0x100940ab: ?GetWorldMatrix@e3_NODE@@UAG_NPAUMATRIX3D@@I@Z
static inline bool Node_GetWorldMatrix(P_e3_NODE node, DE_MATRIX3D* mat) {
    if (!node) return false;
    void** vt = *(void***)node;
    typedef bool (__stdcall *Func)(void*, void*, unsigned int);
    Func f = (Func)vt[40];
    return f(node, mat, 1);
}

static inline P_e3_MESH Node_GetMesh(P_e3_NODE node) {
    if (!node || IsBadReadPtr(node, 0x70)) return NULL;
    return *(P_e3_MESH*)((char*)node + 0x60);
}

// Safe check: verify that an object pointer is an e3_mesh by checking vt[5] -> GetInterfaceName
static inline bool IsMeshObject(void* obj) {
    if (!obj || IsBadReadPtr(obj, sizeof(void*))) return false;
    void** vt = *(void***)obj;
    if (!vt || IsBadReadPtr(vt, 10 * sizeof(void*))) return false;
    void* pFunc = vt[5]; // ?GetInterfaceName@TOBJ3D@@UAGPBDXZ
    if (!pFunc || IsBadCodePtr((FARPROC)pFunc)) return false;

    typedef const char* (__stdcall *GetNameFunc)(void*);
    GetNameFunc getName = (GetNameFunc)pFunc;
    const char* name = getName(obj);
    if (!name || IsBadReadPtr(name, 7)) return false;
    return (strcmp(name, "e3_mesh") == 0);
}

static inline void Mesh_SetMaterial(P_e3_MESH mesh, P_e3_MATERIAL mat) {
    if (!mesh) return;
    void** vt = *(void***)mesh;
    typedef unsigned int (__stdcall *Func)(void*, void*);
    Func f = (Func)vt[60];
    f(mesh, mat);
}

static inline void Mesh_SetFaceSize(P_e3_MESH mesh, unsigned int faceIdx, unsigned short vertCount) {
    if (!mesh) return;
    void** vt = *(void***)mesh;
    typedef void* (__stdcall *Func)(void*, unsigned int, unsigned short);
    Func f = (Func)vt[64];
    f(mesh, faceIdx, vertCount);
}

static inline void Mesh_CreatePoints(P_e3_MESH mesh, unsigned int count) {
    if (!mesh) return;
    void** vt = *(void***)mesh;
    typedef unsigned int (__stdcall *Func)(void*, unsigned int, bool);
    Func f = (Func)vt[69];
    f(mesh, count, true);
}

static inline void Mesh_CreateTPoints(P_e3_MESH mesh, unsigned int count) {
    if (!mesh) return;
    void** vt = *(void***)mesh;
    typedef unsigned int (__stdcall *Func)(void*, unsigned int, bool);
    Func f = (Func)vt[70];
    f(mesh, count, true);
}

static inline void Mesh_CreateNPoints(P_e3_MESH mesh, unsigned int count) {
    if (!mesh) return;
    void** vt = *(void***)mesh;
    typedef unsigned int (__stdcall *Func)(void*, unsigned int, bool);
    Func f = (Func)vt[71];
    f(mesh, count, true);
}

static inline void Mesh_CreateFaces(P_e3_MESH mesh, unsigned int count) {
    if (!mesh) return;
    void** vt = *(void***)mesh;
    typedef unsigned int (__stdcall *Func)(void*, unsigned int, int, bool);
    Func f = (Func)vt[73];
    f(mesh, count, -1, true);
}

static inline void Mesh_CreateTFaces(P_e3_MESH mesh) {
    if (!mesh) return;
    void** vt = *(void***)mesh;
    typedef unsigned int (__stdcall *Func)(void*, bool, bool);
    Func f = (Func)vt[74];
    f(mesh, true, true);
}

static inline void Mesh_CreateNFaces(P_e3_MESH mesh) {
    if (!mesh) return;
    void** vt = *(void***)mesh;
    typedef unsigned int (__stdcall *Func)(void*, bool, bool);
    Func f = (Func)vt[75];
    f(mesh, true, true);
}

static inline void Mesh_CreateDefTFaces(P_e3_MESH mesh) {
    if (!mesh) return;
    void** vt = *(void***)mesh;
    typedef unsigned int (__stdcall *Func)(void*);
    Func f = (Func)vt[77];
    f(mesh);
}

static inline void Mesh_CreateFaceMaterials(P_e3_MESH mesh) {
    if (!mesh) return;
    void** vt = *(void***)mesh;
    typedef unsigned int (__stdcall *Func)(void*);
    Func f = (Func)vt[78];
    f(mesh);
}

static inline void Material_SetNameA(P_e3_MATERIAL mat, const char* name) {
    if (!mat) return;
    void** vt = *(void***)mat;
    typedef bool (__stdcall *Func)(void*, const char*);
    Func f = (Func)vt[21];
    f(mat, name);
}

static inline void Material_SetParam(P_e3_MATERIAL mat, int paramId, unsigned int value, unsigned int type) {
    if (!mat) return;
    void** vt = *(void***)mat;
    typedef unsigned int (__stdcall *Func)(void*, int, unsigned int, unsigned int);
    Func f = (Func)vt[9];
    f(mat, paramId, value, type);
}

static inline unsigned int Material_GetParam(P_e3_MATERIAL mat, int paramId, unsigned int value, unsigned int type) {
    if (!mat) return 0;
    void** vt = *(void***)mat;
    typedef unsigned int (__stdcall *Func)(void*, int, unsigned int, unsigned int);
    Func f = (Func)vt[10];
    return f(mat, paramId, value, type);
}

// -------------------------------------------------------------
// Plugin Import Implementation
// -------------------------------------------------------------

// Map to preserve original transformation matrices (animation pivots)
static std::map<void*, std::vector<float>> s_originalMatrix;
static std::vector<float> s_lastImportedMatrix;
static bool s_hasLastImportedMatrix = false;

static unsigned int DoImport(P_e3_STREAM stream, P_e3_SCENE scene) {
    wchar_t filePath[MAX_PATH] = {0};
    bool resW = Stream_GetFileNameW(stream, filePath, MAX_PATH);
    PluginLog("Stream_GetFileNameW returned %d: '%ls'\n", (int)resW, filePath);

    O3D_Model model;
    bool loaded = false;
    if (filePath[0] != L'\0') {
        if (ReadO3DFile(filePath, model)) {
            loaded = true;
            PluginLog("DoImport: Loaded from file successfully, vertices=%u, faces=%u\n",
                      (unsigned int)model.vertices.size(), (unsigned int)model.faces.size());
        } else {
            PluginLog("DoImport: ReadO3DFile failed for '%ls'\n", filePath);
        }
    }

    if (!loaded) {
        PluginLog("DoImport: Model not loaded, returning 0\n");
        return 0;
    }

    // Extract node name from file name
    char nodeName[MAX_PATH] = "O3D_Mesh";
    const wchar_t* pSlash = wcsrchr(filePath, L'\\');
    if (!pSlash) pSlash = wcsrchr(filePath, L'/');
    if (pSlash) {
        wchar_t wBase[MAX_PATH];
        wcscpy(wBase, pSlash + 1);
        wchar_t* pDot = wcsrchr(wBase, L'.');
        if (pDot) *pDot = L'\0';
        WideCharToMultiByte(CP_ACP, 0, wBase, -1, nodeName, MAX_PATH, NULL, NULL);
    }

    // Create child node with mesh in scene
    P_e3_NODE node = Scene_CreateMeshChild(scene, nodeName);
    if (!node) {
        PluginLog("DoImport: Scene_CreateMeshChild failed\n");
        return 0;
    }

    P_e3_MESH mesh = Node_GetMesh(node);
    if (!mesh) {
        PluginLog("DoImport: Node_GetMesh failed\n");
        return 0;
    }

    // 1. Create and configure materials in scene
    std::vector<P_e3_MATERIAL> deMaterials;
    wchar_t modelDir[MAX_PATH] = {0};
    wcscpy(modelDir, filePath);
    wchar_t* pSep = wcsrchr(modelDir, L'\\');
    if (!pSep) pSep = wcsrchr(modelDir, L'/');
    if (pSep) *pSep = L'\0';

    if (model.materials.empty()) {
        P_e3_MATERIAL mat = Scene_CreateMaterial(scene);
        if (mat) {
            Material_SetNameA(mat, "DefaultMaterial");
            float defDiff[3] = { 0.8f, 0.8f, 0.8f };
            Material_SetParam(mat, 0xc9, (unsigned int)defDiff, 1);
            deMaterials.push_back(mat);
        }
    } else {
        for (size_t m = 0; m < model.materials.size(); m++) {
            const auto& o3dMat = model.materials[m];
            P_e3_MATERIAL mat = Scene_CreateMaterial(scene);
            if (mat) {
                char mName[MAX_PATH];
                if (!o3dMat.texFileName.empty()) {
                    const char* pSlashM = strrchr(o3dMat.texFileName.c_str(), '\\');
                    if (!pSlashM) pSlashM = strrchr(o3dMat.texFileName.c_str(), '/');
                    const char* base = pSlashM ? (pSlashM + 1) : o3dMat.texFileName.c_str();
                    strncpy(mName, base, sizeof(mName) - 1);
                    mName[sizeof(mName) - 1] = '\0';
                } else {
                    sprintf(mName, "Material_%u", (unsigned int)m);
                }
                Material_SetNameA(mat, mName);

                // Diffuse Color (0xc9, type 1: 3 floats)
                Material_SetParam(mat, 0xc9, (unsigned int)o3dMat.faceColor, 1);

                // Opacity / Alpha (0xd2, type 3: 1 float)
                float alpha = o3dMat.faceColor[3];
                Material_SetParam(mat, 0xd2, (unsigned int)&alpha, 3);

                // Specular Color (0xca, type 1: 3 floats)
                Material_SetParam(mat, 0xca, (unsigned int)o3dMat.specularColor, 1);

                // Emissive Color (0xd7, type 1: 3 floats)
                Material_SetParam(mat, 0xd7, (unsigned int)o3dMat.emissiveColor, 1);

                // Specular Power (0xe0, type 3: 1 float)
                Material_SetParam(mat, 0xe0, (unsigned int)&o3dMat.power, 3);

                // Texture map (0x4008: string, 0x4001: flags)
                if (!o3dMat.texFileName.empty()) {
                    std::string finalTexPath = o3dMat.texFileName;
                    if (modelDir[0] != L'\0') {
                        wchar_t candidate[MAX_PATH];
                        // 1. Check same directory
                        swprintf(candidate, MAX_PATH, L"%ls\\%hs", modelDir, o3dMat.texFileName.c_str());
                        if (GetFileAttributesW(candidate) != INVALID_FILE_ATTRIBUTES) {
                            char cBuf[MAX_PATH];
                            WideCharToMultiByte(CP_ACP, 0, candidate, -1, cBuf, MAX_PATH, NULL, NULL);
                            finalTexPath = cBuf;
                        } else {
                            // 2. Check Texture/ or ../Texture or ../../Texture
                            const wchar_t* subDirs[] = {
                                L"%ls\\Texture\\%hs",
                                L"%ls\\..\\Texture\\%hs",
                                L"%ls\\..\\..\\Texture\\%hs",
                                L"%ls\\texture\\%hs",
                                L"%ls\\..\\texture\\%hs",
                                L"%ls\\..\\..\\texture\\%hs"
                            };
                            for (const auto* fmt : subDirs) {
                                swprintf(candidate, MAX_PATH, fmt, modelDir, o3dMat.texFileName.c_str());
                                if (GetFileAttributesW(candidate) != INVALID_FILE_ATTRIBUTES) {
                                    char cBuf[MAX_PATH];
                                    WideCharToMultiByte(CP_ACP, 0, candidate, -1, cBuf, MAX_PATH, NULL, NULL);
                                    finalTexPath = cBuf;
                                    break;
                                }
                            }
                        }
                    }
                    Material_SetParam(mat, 0x4008, (unsigned int)finalTexPath.c_str(), 0xc);
                    unsigned int flags = Material_GetParam(mat, 0x4001, 0, 1);
                    Material_SetParam(mat, 0x4001, flags | 8, 1); // Enable texture mapping
                }

                deMaterials.push_back(mat);
            }
        }
    }

    uint32_t numVerts = (uint32_t)model.vertices.size();
    uint32_t numFaces = (uint32_t)model.faces.size();

    // Allocate geometry buffers
    Mesh_CreatePoints(mesh, numVerts);
    Mesh_CreateTPoints(mesh, numVerts);
    Mesh_CreateNPoints(mesh, numVerts);
    Mesh_CreateFaces(mesh, numFaces);
    Mesh_CreateFaceMaterials(mesh);

    DE_POINT3D* pPoints = *(DE_POINT3D**)((char*)mesh + 0x54);
    DE_POINT2D* pUVs    = *(DE_POINT2D**)((char*)mesh + 0x60);
    DE_POINT3D* pNorms  = *(DE_POINT3D**)((char*)mesh + 0x68);

    // Pivot handling:
    // Section 0x79 in OMSI stores the pivot origin (e.g. wheel axle center, door hinge).
    // The vertex positions in OMSI are in vehicle world coordinates.
    // In Deep Exploration:
    // We set the node's transform matrix to dePivotMat.
    // To keep vertices in their exact vehicle world position when rendered (V_world = V_local * dePivotMat),
    // the local mesh vertices must be relative to the pivot: V_local = V_world * invPivotMat.
    DE_MATRIX3D dePivotMat;
    Matrix4x4_Identity(&dePivotMat);
    bool hasPivot = false;

    if (model.has_matrix) {
        OmsiMatrixToDEMatrix(model.matrix, &dePivotMat);
        hasPivot = !Matrix4x4_IsIdentity(&dePivotMat);
    }

    DE_MATRIX3D invPivotMat;
    Matrix4x4_Identity(&invPivotMat);
    if (hasPivot) {
        Matrix4x4_InvertAffine(&dePivotMat, &invPivotMat);
        PluginLog("DoImport: Non-identity pivot detected: translation=(%.4f, %.4f, %.4f)\n",
                  dePivotMat.m[3][0], dePivotMat.m[3][1], dePivotMat.m[3][2]);
    }

    for (uint32_t i = 0; i < numVerts; i++) {
        // Coordinate transformation:
        // OMSI (Left-Handed: X=Right, Y=Up, Z=Forward) ->
        // Deep Exploration (Right-Handed: X=Right, Y=Forward, Z=Up)
        double wx = model.vertices[i].position.x;
        double wy = model.vertices[i].position.z; // Forward -> Y
        double wz = model.vertices[i].position.y; // Up -> Z

        double wnx = model.vertices[i].normal.x;
        double wny = model.vertices[i].normal.z;
        double wnz = model.vertices[i].normal.y;

        if (hasPivot) {
            // Transform from world to local coordinates relative to pivot
            double lx, ly, lz;
            Matrix4x4_TransformPoint(&invPivotMat, wx, wy, wz, lx, ly, lz);
            wx = lx; wy = ly; wz = lz;

            double lnx, lny, lnz;
            Matrix4x4_TransformNormal(&invPivotMat, wnx, wny, wnz, lnx, lny, lnz);
            wnx = lnx; wny = lny; wnz = lnz;
        }

        if (pPoints) {
            pPoints[i].x = (float)wx;
            pPoints[i].y = (float)wy;
            pPoints[i].z = (float)wz;
        }
        if (pNorms) {
            pNorms[i].x = (float)wnx;
            pNorms[i].y = (float)wny;
            pNorms[i].z = (float)wnz;
        }
        if (pUVs) {
            // Vertical unwrap mirroring fix (DirectX V=0 top vs Deep Exploration V=0 bottom):
            pUVs[i].u = model.vertices[i].uv.x;
            pUVs[i].v = 1.0f - model.vertices[i].uv.y;
        }
    }

    // Set up face vertices and material assignments
    // Note: swap b <-> c because coordinate system reflection det = -1 reverses winding
    int** pFaces             = *(int***)((char*)mesh + 0x90);
    P_e3_MATERIAL* pFaceMats = *(P_e3_MATERIAL**)((char*)mesh + 0x8c);
    for (uint32_t i = 0; i < numFaces; i++) {
        Mesh_SetFaceSize(mesh, i, 3);
        if (pFaces && pFaces[i]) {
            pFaces[i][0] = (int)model.faces[i].a;
            pFaces[i][1] = (int)model.faces[i].c;
            pFaces[i][2] = (int)model.faces[i].b;
        }
        if (pFaceMats) {
            uint16_t mId = model.faces[i].material_id;
            if (mId < deMaterials.size()) {
                pFaceMats[i] = deMaterials[mId];
            } else if (!deMaterials.empty()) {
                pFaceMats[i] = deMaterials[0];
            } else {
                pFaceMats[i] = NULL;
            }
        }
    }

    // Link UVs and normals to faces
    Mesh_CreateTFaces(mesh);
    Mesh_CreateNFaces(mesh);
    Mesh_CreateDefTFaces(mesh);

    int** pTFaces = *(int***)((char*)mesh + 0x94);
    int** pNFaces = *(int***)((char*)mesh + 0x98);
    for (uint32_t i = 0; i < numFaces; i++) {
        if (pTFaces && pTFaces[i]) {
            pTFaces[i][0] = (int)model.faces[i].a;
            pTFaces[i][1] = (int)model.faces[i].c;
            pTFaces[i][2] = (int)model.faces[i].b;
        }
        if (pNFaces && pNFaces[i]) {
            pNFaces[i][0] = (int)model.faces[i].a;
            pNFaces[i][1] = (int)model.faces[i].c;
            pNFaces[i][2] = (int)model.faces[i].b;
        }
    }

    // Assign default fallback material to mesh without deleting pFaceMats
    if (!deMaterials.empty()) {
        *(P_e3_MATERIAL*)((char*)mesh + 0x50) = deMaterials[0];
    }

    // Apply the node matrix (vt[38])
    if (hasPivot) {
        Node_SetMatrix(node, &dePivotMat, false);
    } else {
        DE_MATRIX3D ident;
        Matrix4x4_Identity(&ident);
        Node_SetMatrix(node, &ident, false);
    }

    // Back up original matrix
    if (model.has_matrix) {
        std::vector<float> matVec(model.matrix, model.matrix + 16);
        s_originalMatrix[node] = matVec;
        s_originalMatrix[mesh] = matVec;
        s_lastImportedMatrix = matVec;
        s_hasLastImportedMatrix = true;
    }

    PluginLog("DoImport: Successfully imported %u vertices, %u faces, %u materials (hasPivot=%d)\n",
              numVerts, numFaces, (unsigned int)model.materials.size(), (int)hasPivot);
    return 1;
}

// -------------------------------------------------------------
// Plugin Export Implementation
// -------------------------------------------------------------

struct ExportContext {
    O3D_Model* model;
    P_e3_SCENE scene;
    std::map<P_e3_MATERIAL, uint16_t> matMap;
};

static void ReadMaterialProperties(P_e3_MATERIAL mat, O3D_Material& outMat) {
    if (!mat || IsBadReadPtr(mat, sizeof(void*))) return;
    void** vt = *(void***)mat;
    if (!vt || IsBadReadPtr(vt, 12 * sizeof(void*))) return;

    float diff[3] = {0.8f, 0.8f, 0.8f};
    if (Material_GetParam(mat, 0xc9, (unsigned int)diff, 1)) {
        outMat.faceColor[0] = diff[0];
        outMat.faceColor[1] = diff[1];
        outMat.faceColor[2] = diff[2];
    }
    float alpha = 1.0f;
    if (Material_GetParam(mat, 0xd2, (unsigned int)&alpha, 3)) {
        outMat.faceColor[3] = alpha;
    }
    float spec[3] = {0};
    if (Material_GetParam(mat, 0xca, (unsigned int)spec, 1)) {
        outMat.specularColor[0] = spec[0];
        outMat.specularColor[1] = spec[1];
        outMat.specularColor[2] = spec[2];
    }
    float emis[3] = {0};
    if (Material_GetParam(mat, 0xd7, (unsigned int)emis, 1)) {
        outMat.emissiveColor[0] = emis[0];
        outMat.emissiveColor[1] = emis[1];
        outMat.emissiveColor[2] = emis[2];
    }
    float pwr = 1.0f;
    if (Material_GetParam(mat, 0xe0, (unsigned int)&pwr, 3)) {
        outMat.power = pwr;
    }
    char texBuf[MAX_PATH] = {0};
    if (Material_GetParam(mat, 0x4008, (unsigned int)texBuf, 0xc) && texBuf[0]) {
        const char* pSlash = strrchr(texBuf, '\\');
        if (!pSlash) pSlash = strrchr(texBuf, '/');
        outMat.texFileName = pSlash ? (pSlash + 1) : texBuf;
    }
}

struct VertexKey {
    int pos_idx;
    int norm_idx;
    int uv_idx;

    bool operator<(const VertexKey& o) const {
        if (pos_idx != o.pos_idx) return pos_idx < o.pos_idx;
        if (uv_idx != o.uv_idx) return uv_idx < o.uv_idx;
        return norm_idx < o.norm_idx;
    }
};

static void ExportMeshNode(P_e3_NODE node, ExportContext* ctx) {
    if (!node || !ctx || !ctx->model) return;
    if (IsBadReadPtr(node, 0x70)) return;

    void* obj = *(void**)((char*)node + 0x60);
    if (!obj || !IsMeshObject(obj)) {
        // Skip cameras, lights, dummy objects, etc.
        return;
    }

    P_e3_MESH mesh = (P_e3_MESH)obj;
    if (IsBadReadPtr(mesh, 0xb0)) return;

    int numFaces   = *(int*)((char*)mesh + 0xa4);
    int numPoints  = *(int*)((char*)mesh + 0x58);
    int numTPoints = *(int*)((char*)mesh + 0x64);
    int numNPoints = *(int*)((char*)mesh + 0x6c);

    DE_POINT3D* pPoints = *(DE_POINT3D**)((char*)mesh + 0x54);
    DE_POINT2D* pUVs    = *(DE_POINT2D**)((char*)mesh + 0x60);
    DE_POINT3D* pNorms  = *(DE_POINT3D**)((char*)mesh + 0x68);
    int** pFaces        = *(int***)((char*)mesh + 0x90);
    int** pTFaces       = *(int***)((char*)mesh + 0x94);
    int** pNFaces       = *(int***)((char*)mesh + 0x98);
    P_e3_MATERIAL* pFaceMats = *(P_e3_MATERIAL**)((char*)mesh + 0x8c);
    P_e3_MATERIAL defaultMat = *(P_e3_MATERIAL*)((char*)mesh + 0x50);

    if (numFaces <= 0 || numPoints <= 0 || !pPoints || !pFaces) return;
    if (IsBadReadPtr(pPoints, sizeof(DE_POINT3D) * numPoints)) return;
    if (IsBadReadPtr(pFaces, sizeof(int*) * numFaces)) return;

    if (pUVs && (numTPoints <= 0 || IsBadReadPtr(pUVs, sizeof(DE_POINT2D)))) {
        pUVs = NULL;
        numTPoints = 0;
    }
    if (pNorms && (numNPoints <= 0 || IsBadReadPtr(pNorms, sizeof(DE_POINT3D)))) {
        pNorms = NULL;
        numNPoints = 0;
    }
    if (pTFaces && IsBadReadPtr(pTFaces, sizeof(int*) * numFaces)) {
        pTFaces = NULL;
    }
    if (pNFaces && IsBadReadPtr(pNFaces, sizeof(int*) * numFaces)) {
        pNFaces = NULL;
    }
    if (pFaceMats && IsBadReadPtr(pFaceMats, sizeof(P_e3_MATERIAL) * numFaces)) {
        pFaceMats = NULL;
    }

    // Retrieve transform matrix from node
    DE_MATRIX3D nodeTM;
    Matrix4x4_Identity(&nodeTM);
    bool hasTM = Node_GetMatrix(node, &nodeTM, true);
    bool isIdentity = Matrix4x4_IsIdentity(&nodeTM);

    if (!isIdentity) {
        DEMatrixToOmsiMatrix(&nodeTM, ctx->model->matrix);
        ctx->model->has_matrix = true;
        PluginLog("ExportMeshNode: Read non-identity matrix: translation=(%.4f, %.4f, %.4f)\n",
                  nodeTM.m[3][0], nodeTM.m[3][1], nodeTM.m[3][2]);
    } else if (!ctx->model->has_matrix) {
        // Fallback: preserve original imported matrix if node was unchanged
        auto it = s_originalMatrix.find(node);
        if (it == s_originalMatrix.end()) it = s_originalMatrix.find(mesh);
        if (it != s_originalMatrix.end() && it->second.size() == 16) {
            memcpy(ctx->model->matrix, it->second.data(), 16 * sizeof(float));
            ctx->model->has_matrix = true;
        } else if (s_hasLastImportedMatrix && s_lastImportedMatrix.size() == 16) {
            memcpy(ctx->model->matrix, s_lastImportedMatrix.data(), 16 * sizeof(float));
            ctx->model->has_matrix = true;
        }
    }

    // Deduplicate vertices by unique (pos_idx, norm_idx, uv_idx) tuple
    std::map<VertexKey, uint32_t> vertMap;

    auto getOrCreateVertex = [&](int pos_idx, int norm_idx, int uv_idx) -> uint32_t {
        VertexKey key;
        key.pos_idx = pos_idx;
        key.norm_idx = norm_idx;
        key.uv_idx = uv_idx;

        auto it = vertMap.find(key);
        if (it != vertMap.end()) {
            return it->second;
        }

        O3D_Vertex v;

        // Position
        double px = pPoints[pos_idx].x;
        double py = pPoints[pos_idx].y;
        double pz = pPoints[pos_idx].z;

        // Normal
        double nx = 0.0, ny = 0.0, nz = 1.0;
        if (pNorms && norm_idx >= 0 && norm_idx < numNPoints) {
            nx = pNorms[norm_idx].x;
            ny = pNorms[norm_idx].y;
            nz = pNorms[norm_idx].z;
        }

        // Apply node transformation to world space
        if (!isIdentity) {
            double tx, ty, tz;
            Matrix4x4_TransformPoint(&nodeTM, px, py, pz, tx, ty, tz);
            px = tx; py = ty; pz = tz;

            double tnx, tny, tnz;
            Matrix4x4_TransformNormal(&nodeTM, nx, ny, nz, tnx, tny, tnz);
            nx = tnx; ny = tny; nz = tnz;
        }

        // Deep Exploration (X=Right, Y=Forward, Z=Up) -> OMSI (X=Right, Y=Up, Z=Forward)
        v.position.x = (float)px;
        v.position.y = (float)pz;
        v.position.z = (float)py;

        v.normal.x = (float)nx;
        v.normal.y = (float)nz;
        v.normal.z = (float)ny;

        // UV
        if (pUVs && uv_idx >= 0 && uv_idx < numTPoints) {
            v.uv.x = pUVs[uv_idx].u;
            v.uv.y = 1.0f - pUVs[uv_idx].v; // Invert V back for DirectX / OMSI
        } else {
            v.uv.x = 0.0f;
            v.uv.y = 0.0f;
        }

        uint32_t newIdx = (uint32_t)ctx->model->vertices.size();
        ctx->model->vertices.push_back(v);
        vertMap[key] = newIdx;
        return newIdx;
    };

    // Export faces
    for (int f = 0; f < numFaces; f++) {
        if (!pFaces[f] || IsBadReadPtr(pFaces[f], 3 * sizeof(int))) continue;
        int fa = pFaces[f][0];
        int fb = pFaces[f][1];
        int fc = pFaces[f][2];
        if (fa < 0 || fa >= numPoints || fb < 0 || fb >= numPoints || fc < 0 || fc >= numPoints) continue;

        int* pTFace = (pTFaces && !IsBadReadPtr(pTFaces + f, sizeof(int*)) && pTFaces[f] && !IsBadReadPtr(pTFaces[f], 3 * sizeof(int))) ? pTFaces[f] : NULL;
        int* pNFace = (pNFaces && !IsBadReadPtr(pNFaces + f, sizeof(int*)) && pNFaces[f] && !IsBadReadPtr(pNFaces[f], 3 * sizeof(int))) ? pNFaces[f] : NULL;

        int uv0 = pTFace ? pTFace[0] : (pUVs && fa < numTPoints ? fa : -1);
        int uv1 = pTFace ? pTFace[1] : (pUVs && fb < numTPoints ? fb : -1);
        int uv2 = pTFace ? pTFace[2] : (pUVs && fc < numTPoints ? fc : -1);

        int n0 = pNFace ? pNFace[0] : (pNorms && fa < numNPoints ? fa : -1);
        int n1 = pNFace ? pNFace[1] : (pNorms && fb < numNPoints ? fb : -1);
        int n2 = pNFace ? pNFace[2] : (pNorms && fc < numNPoints ? fc : -1);

        // Face winding: swap 1 <-> 2 for OMSI coordinate reflection
        // In DE: corner 0 = a, corner 1 = c, corner 2 = b
        // In OMSI: face.a = corner 0, face.b = corner 2, face.c = corner 1
        uint32_t idx_a = getOrCreateVertex(fa, n0, uv0);
        uint32_t idx_b = getOrCreateVertex(fc, n2, uv2);
        uint32_t idx_c = getOrCreateVertex(fb, n1, uv1);

        O3D_Face face;
        face.a = idx_a;
        face.b = idx_b;
        face.c = idx_c;

        P_e3_MATERIAL fMat = (pFaceMats && !IsBadReadPtr(pFaceMats + f, sizeof(P_e3_MATERIAL))) ? pFaceMats[f] : NULL;
        if (!fMat) fMat = defaultMat;

        uint16_t matId = 0;
        if (fMat) {
            auto it = ctx->matMap.find(fMat);
            if (it != ctx->matMap.end()) {
                matId = it->second;
            } else {
                matId = (uint16_t)ctx->model->materials.size();
                O3D_Material outMat;
                outMat.faceColor[0] = 0.8f; outMat.faceColor[1] = 0.8f; outMat.faceColor[2] = 0.8f; outMat.faceColor[3] = 1.0f;
                outMat.specularColor[0] = 0.0f; outMat.specularColor[1] = 0.0f; outMat.specularColor[2] = 0.0f;
                outMat.emissiveColor[0] = 0.0f; outMat.emissiveColor[1] = 0.0f; outMat.emissiveColor[2] = 0.0f;
                outMat.power = 1.0f;
                ReadMaterialProperties(fMat, outMat);
                ctx->model->materials.push_back(outMat);
                ctx->matMap[fMat] = matId;
            }
        }
        face.material_id = matId;
        ctx->model->faces.push_back(face);
    }

    PluginLog("ExportMeshNode: Exported mesh with %u unified vertices and %u faces\n",
              (unsigned int)vertMap.size(), numFaces);
}

// Safe recursive scene traversal
static void TraverseNode(P_e3_NODE node, ExportContext* ctx) {
    if (!node || IsBadReadPtr(node, 0x70)) return;

    ExportMeshNode(node, ctx);

    // Traverse children
    P_e3_NODE child = *(P_e3_NODE*)((char*)node + 0x4c);
    int safetyCount = 0;
    while (child && safetyCount++ < 10000) {
        if (IsBadReadPtr(child, 0x50)) break;
        TraverseNode(child, ctx);
        child = *(P_e3_NODE*)((char*)child + 0x44); // next sibling
    }
}

static void TraverseScene(P_e3_SCENE scene, ExportContext* ctx) {
    if (!scene || IsBadReadPtr(scene, 0x60)) return;
    P_e3_NODE child = *(P_e3_NODE*)((char*)scene + 0x4c);
    int safetyCount = 0;
    while (child && safetyCount++ < 10000) {
        if (IsBadReadPtr(child, 0x50)) break;
        TraverseNode(child, ctx);
        child = *(P_e3_NODE*)((char*)child + 0x44); // next sibling
    }
}

static unsigned int DoExport(P_e3_STREAM stream, P_e3_SCENE scene) {
    wchar_t filePath[MAX_PATH] = {0};
    Stream_GetFileNameW(stream, filePath, MAX_PATH);
    PluginLog("DoExport: Exporting, detected path: '%ls'\n", filePath);

    O3D_Model model;
    model.version = 1; // Export as OMSI Version 1 with short indices
    model.flags = 0;
    model.has_long_tris = false;
    model.has_encryption = false;
    model.product_id = 0xFFFFFFFF;

    ExportContext ctx;
    ctx.model = &model;
    ctx.scene = scene;

    // Traverse all nodes in scene safely without SEH crash
    TraverseScene(scene, &ctx);

    if (model.vertices.empty()) {
        PluginLog("DoExport: No geometry found in scene\n");
        return 0;
    }

    // Ensure Section 0x79 pivot matrix is preserved
    if (!model.has_matrix) {
        if (s_hasLastImportedMatrix && s_lastImportedMatrix.size() == 16) {
            memcpy(model.matrix, s_lastImportedMatrix.data(), 16 * sizeof(float));
            model.has_matrix = true;
        } else {
            memset(model.matrix, 0, sizeof(model.matrix));
            model.matrix[0] = model.matrix[5] = model.matrix[10] = model.matrix[15] = 1.0f;
            model.has_matrix = true;
        }
    }

    // Ensure all referenced materials exist
    int maxMatId = 0;
    for (const auto& face : model.faces) {
        if (face.material_id > maxMatId) maxMatId = face.material_id;
    }

    if (model.materials.empty()) {
        model.materials.resize(maxMatId + 1);
        for (int m = 0; m <= maxMatId; m++) {
            model.materials[m].faceColor[0] = 0.8f;
            model.materials[m].faceColor[1] = 0.8f;
            model.materials[m].faceColor[2] = 0.8f;
            model.materials[m].faceColor[3] = 1.0f;
            model.materials[m].specularColor[0] = 0.0f;
            model.materials[m].specularColor[1] = 0.0f;
            model.materials[m].specularColor[2] = 0.0f;
            model.materials[m].emissiveColor[0] = 0.0f;
            model.materials[m].emissiveColor[1] = 0.0f;
            model.materials[m].emissiveColor[2] = 0.0f;
            model.materials[m].power = 1.0f;
        }
    } else if ((int)model.materials.size() <= maxMatId) {
        size_t oldSize = model.materials.size();
        model.materials.resize(maxMatId + 1);
        for (size_t m = oldSize; m <= (size_t)maxMatId; m++) {
            model.materials[m] = model.materials[0];
        }
    }

    std::vector<uint8_t> outBytes;
    if (!SerializeO3D(model, outBytes)) {
        PluginLog("DoExport: SerializeO3D failed\n");
        return 0;
    }

    unsigned int written = Stream_Write(stream, outBytes.data(), (unsigned int)outBytes.size());
    PluginLog("DoExport: Stream_Write wrote %u of %u bytes\n", written, (unsigned int)outBytes.size());

    if (written != outBytes.size() && filePath[0] != L'\0') {
        FILE* f = _wfopen(filePath, L"wb");
        if (f) {
            fwrite(outBytes.data(), 1, outBytes.size(), f);
            fclose(f);
            PluginLog("DoExport: Fallback _wfopen wrote %u bytes\n", (unsigned int)outBytes.size());
        }
    }

    PluginLog("DoExport: Successfully exported %u vertices, %u faces (%u bytes)\n",
              (unsigned int)model.vertices.size(), (unsigned int)model.faces.size(), (unsigned int)outBytes.size());
    return 1;
}

// -------------------------------------------------------------
// Format Callback function (called by Deep Exploration for actions)
// -------------------------------------------------------------

static unsigned int __stdcall O3D_Callback(unsigned int action, void* stream, void* scene, void* options) {
    PluginLog("O3D_Callback: action=%u, stream=%p, scene=%p, options=%p\n", action, stream, scene, options);
    switch (action) {
        case 1: // Import / Open
            return DoImport((P_e3_STREAM)stream, (P_e3_SCENE)scene);

        case 2: // Export / Save
            return DoExport((P_e3_STREAM)stream, (P_e3_SCENE)scene);

        case 14: // Custom format UI XML string
            return 0;

        default:
            break;
    }
    return 0;
}

// -------------------------------------------------------------
// Plugin Entry Point Export: E3DLLFunc
// -------------------------------------------------------------

extern "C" __declspec(dllexport) unsigned int __stdcall E3DLLFunc(unsigned int msg, void* arg2, void* arg3) {
    PluginLog("E3DLLFunc: msg=0x%x (%u), arg2=%p, arg3=%p\n", msg, msg, arg2, arg3);

    switch (msg) {
        case 0x1010: // Query Plugin Capabilities
            return 0x95; // Standard 3D File Format Plugin

        case 0x1020: // Query Version String
            if (arg2) {
                wcscpy((wchar_t*)arg2, L"1.0.0 OMSI 2 O3D Importer/Exporter");
                return 1;
            }
            break;

        case 0x1021: // Query Description String
            if (arg2) {
                wcscpy((wchar_t*)arg2, L"OMSI 2 3D Model");
                return 1;
            }
            break;

        case 0x1001: { // Register Format
            P_e3_HOST host = (P_e3_HOST)arg2;
            void* cookie = arg3;
            if (!host) return 0;

            void** host_vt = *(void***)host;

            typedef void* (__stdcall *Func2)(void*, void*);
            Func2 getCookie2 = (Func2)host_vt[2];
            void* cookie2 = getCookie2(host, cookie);

            DE_FILETYPE_REG reg;
            memset(&reg, 0, sizeof(reg));
            reg.size = 0x52; // 82 bytes
            reg.flags = 0x23c01; // Can import (0x1), Can export (0x20000), 3D format (0x800), Mesh (0x400), Materials (0x2000), Has Options (0x1000)
            reg.ext = L"o3d";
            reg.desc = L"OMSI 2 3D Model";
            reg.guid1 = O3D_TYPE_GUID;
            reg.cookie = (DWORD)cookie;
            reg.optName = NULL;
            reg.callback = (void*)O3D_Callback;
            reg.e3_type = O3D_TYPE_GUID; // Non-zero GUID required by Deep Exploration

            typedef unsigned int (__stdcall *RegisterFunc)(void*, void*);
            RegisterFunc registerType = (RegisterFunc)host_vt[10];
            unsigned int res = registerType(host, &reg);
            PluginLog("E3DLLFunc: Registered O3D format with code %u\n", res);
            return 5;
        }

        default:
            break;
    }

    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_hModule = (HMODULE)hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}
