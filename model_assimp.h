#ifndef _MODEL_ASSIMP_H_
#define _MODEL_ASSIMP_H_
#include <stdint.h>
qboolean Assimp_Init(void);
void Assimp_Shutdown(void);

#define C_STRUCT struct
#define C_ENUM enum
#define AI_MAX_NUMBER_OF_COLOR_SETS 0x8
#define AI_MAX_NUMBER_OF_TEXTURECOORDS 0x8

struct aiFileIO;
struct aiFile;

typedef enum aiReturn {
	aiReturn_SUCCESS = 0x0,
	aiReturn_FAILURE = -0x1,
	aiReturn_OUTOFMEMORY = -0x3,
	_AI_ENFORCE_ENUM_SIZE = 0x7fffffff
} aiReturn;

enum aiOrigin {
	aiOrigin_SET = 0x0,
	aiOrigin_CUR = 0x1,
	aiOrigin_END = 0x2,
	_AI_ORIGIN_ENFORCE_ENUM_SIZE = 0x7fffffff
};

// aiFile callbacks
typedef size_t          (*aiFileWriteProc) (C_STRUCT aiFile*,   const char*, size_t, size_t);
typedef size_t          (*aiFileReadProc)  (C_STRUCT aiFile*,   char*, size_t,size_t);
typedef size_t          (*aiFileTellProc)  (C_STRUCT aiFile*);
typedef void            (*aiFileFlushProc) (C_STRUCT aiFile*);
typedef C_ENUM aiReturn (*aiFileSeek)      (C_STRUCT aiFile*, size_t, C_ENUM aiOrigin);

// aiFileIO callbacks
typedef C_STRUCT aiFile* (*aiFileOpenProc)  (C_STRUCT aiFileIO*, const char*, const char*);
typedef void             (*aiFileCloseProc) (C_STRUCT aiFileIO*, C_STRUCT aiFile*);

// Represents user-defined data
typedef char* aiUserData;

struct aiFileIO
{
	aiFileOpenProc OpenProc;
	aiFileCloseProc CloseProc;
	aiUserData UserData;
};

struct aiFile {
	aiFileReadProc ReadProc;
	aiFileWriteProc WriteProc;
	aiFileTellProc TellProc;
	aiFileTellProc FileSizeProc;
	aiFileSeek SeekProc;
	aiFileFlushProc FlushProc;
	aiUserData UserData;
};

enum aiAnimBehaviour {
	aiAnimBehaviour_DEFAULT = 0x0,
	aiAnimBehaviour_CONSTANT = 0x1,
	aiAnimBehaviour_LINEAR = 0x2,
	aiAnimBehaviour_REPEAT = 0x3,
#ifndef SWIG
	_aiAnimBehaviour_Force32Bit = INT_MAX
#endif
};

struct aiScene;
struct aiAnimation;
struct aiMesh;
struct aiNode;
struct aiMaterial;
struct aiMetadata;

typedef float ai_real;

#define MAXLEN 1024
typedef uint32_t ai_uint32;

struct aiString {
	ai_uint32 length;
	char data[MAXLEN];
};

struct aiVertexWeight {
	unsigned int mVertexId;
	ai_real mWeight;
};

struct aiMatrix4x4 {
	ai_real a1, a2, a3, a4;
	ai_real b1, b2, b3, b4;
	ai_real c1, c2, c3, c4;
	ai_real d1, d2, d3, d4;
};

struct aiNode
{
	C_STRUCT aiString mName;
	C_STRUCT aiMatrix4x4 mTransformation;
	C_STRUCT aiNode* mParent;
	unsigned int mNumChildren;
	C_STRUCT aiNode** mChildren;
	unsigned int mNumMeshes;
	unsigned int* mMeshes;
	C_STRUCT aiMetadata* mMetaData;
};

struct aiBone {
	C_STRUCT aiString mName;
	unsigned int mNumWeights;
#ifndef ASSIMP_BUILD_NO_ARMATUREPOPULATE_PROCESS
	C_STRUCT aiNode *mArmature;
	C_STRUCT aiNode *mNode;
#endif
	C_STRUCT aiVertexWeight *mWeights;
	C_STRUCT aiMatrix4x4 mOffsetMatrix;
};

struct aiFace {
	unsigned int mNumIndices;
	unsigned int *mIndices;
};

enum aiPrimitiveType {
	aiPrimitiveType_POINT = 0x1,
	aiPrimitiveType_LINE = 0x2,
	aiPrimitiveType_TRIANGLE = 0x4,
	aiPrimitiveType_POLYGON = 0x8,
	aiPrimitiveType_NGONEncodingFlag = 0x10,
#ifndef SWIG
	_aiPrimitiveType_Force32Bit = INT_MAX
#endif
};

struct aiVector3D {
	ai_real x, y, z;
};

struct aiVectorKey {
	double mTime;
	C_STRUCT aiVector3D mValue;
};

struct aiQuaternion {
	ai_real w, x, y, z;
};

enum aiAnimInterpolation {
	aiAnimInterpolation_Step,
	aiAnimInterpolation_Linear,
	aiAnimInterpolation_Spherical_Linear,
	aiAnimInterpolation_Cubic_Spline,
};

struct aiQuatKey {
	double mTime;
	C_STRUCT aiQuaternion mValue;
	C_ENUM aiAnimInterpolation mInterpolation;
};

struct aiAABB {
	C_STRUCT aiVector3D mMin;
	C_STRUCT aiVector3D mMax;
};

struct aiMesh {
	unsigned int mPrimitiveTypes;
	unsigned int mNumVertices;
	unsigned int mNumFaces;
	C_STRUCT aiVector3D *mVertices;
	C_STRUCT aiVector3D *mNormals;
	C_STRUCT aiVector3D *mTangents;
	C_STRUCT aiVector3D *mBitangents;
	C_STRUCT aiColor4D *mColors[AI_MAX_NUMBER_OF_COLOR_SETS];
	C_STRUCT aiVector3D *mTextureCoords[AI_MAX_NUMBER_OF_TEXTURECOORDS];
	unsigned int mNumUVComponents[AI_MAX_NUMBER_OF_TEXTURECOORDS];
	C_STRUCT aiFace *mFaces;
	unsigned int mNumBones;
	C_STRUCT aiBone **mBones;
	unsigned int mMaterialIndex;
	C_STRUCT aiString mName;
	unsigned int mNumAnimMeshes;
	C_STRUCT aiAnimMesh **mAnimMeshes;
	unsigned int mMethod;
	C_STRUCT aiAABB mAABB;
	C_STRUCT aiString **mTextureCoordsNames;
};

extern const C_STRUCT aiScene *(*qaiImportFileFromMemory)(
		const char *pBuffer,
		unsigned int pLength,
		unsigned int pFlags,
		const char *pHint);

extern void (*qaiReleaseImport)(
		const C_STRUCT aiScene *pScene);

extern const C_STRUCT aiScene *(*qaiImportFileEx)(
		const char *pFile,
		unsigned int pFlags,
		C_STRUCT aiFileIO *pFS);

extern aiReturn *(*qaiExportSceneEx)(const C_STRUCT aiScene *pScene,
		const char *pFormatId,
		const char *pFileName,
		C_STRUCT aiFileIO *pIO,
		unsigned int pPreprocessing);


enum aiPostProcessSteps
{
	aiProcess_CalcTangentSpace = 0x1,
	aiProcess_JoinIdenticalVertices = 0x2,
	aiProcess_MakeLeftHanded = 0x4,
	aiProcess_Triangulate = 0x8,
	aiProcess_RemoveComponent = 0x10,
	aiProcess_GenNormals = 0x20,
	aiProcess_GenSmoothNormals = 0x40,
	aiProcess_SplitLargeMeshes = 0x80,
	aiProcess_PreTransformVertices = 0x100,
	aiProcess_LimitBoneWeights = 0x200,
	aiProcess_ValidateDataStructure = 0x400,
	aiProcess_ImproveCacheLocality = 0x800,
	aiProcess_RemoveRedundantMaterials = 0x1000,
	aiProcess_FixInfacingNormals = 0x2000,
	aiProcess_PopulateArmatureData = 0x4000,
	aiProcess_SortByPType = 0x8000,
	aiProcess_FindDegenerates = 0x10000,
	aiProcess_FindInvalidData = 0x20000,
	aiProcess_GenUVCoords = 0x40000,
	aiProcess_TransformUVCoords = 0x80000,
	aiProcess_FindInstances = 0x100000,
	aiProcess_OptimizeMeshes  = 0x200000,
	aiProcess_OptimizeGraph  = 0x400000,
	aiProcess_FlipUVs = 0x800000,
	aiProcess_FlipWindingOrder  = 0x1000000,
	aiProcess_SplitByBoneCount  = 0x2000000,
	aiProcess_Debone  = 0x4000000,
	aiProcess_GlobalScale = 0x8000000,
	aiProcess_EmbedTextures  = 0x10000000,
	// aiProcess_GenEntityMeshes = 0x100000,
	// aiProcess_OptimizeAnimations = 0x200000
	// aiProcess_FixTexturePaths = 0x200000
	aiProcess_ForceGenNormals = 0x20000000,
	aiProcess_DropNormals = 0x40000000,
	aiProcess_GenBoundingBoxes = 0x80000000
};

struct aiScene
{
	unsigned int mFlags;
	C_STRUCT aiNode* mRootNode;
	unsigned int mNumMeshes;
	C_STRUCT aiMesh** mMeshes;
	unsigned int mNumMaterials;
	C_STRUCT aiMaterial** mMaterials;
	unsigned int mNumAnimations;
	C_STRUCT aiAnimation** mAnimations;
	unsigned int mNumTextures;
	C_STRUCT aiTexture** mTextures;
	unsigned int mNumLights;
	C_STRUCT aiLight** mLights;
	unsigned int mNumCameras;
	C_STRUCT aiCamera** mCameras;
	C_STRUCT aiMetadata* mMetaData;
	C_STRUCT aiString mName;
	/**  Internal data, do not touch */
	char* mPrivate;
};

struct aiNodeAnim {
	C_STRUCT aiString mNodeName;
	unsigned int mNumPositionKeys;
	C_STRUCT aiVectorKey *mPositionKeys;
	unsigned int mNumRotationKeys;
	C_STRUCT aiQuatKey *mRotationKeys;
	unsigned int mNumScalingKeys;
	C_STRUCT aiVectorKey *mScalingKeys;
	C_ENUM aiAnimBehaviour mPreState;
	C_ENUM aiAnimBehaviour mPostState;
};

struct aiAnimation {
	C_STRUCT aiString mName;
	double mDuration;
	double mTicksPerSecond;
	unsigned int mNumChannels;
	C_STRUCT aiNodeAnim **mChannels;
	unsigned int mNumMeshChannels;
	C_STRUCT aiMeshAnim **mMeshChannels;
	unsigned int mNumMorphMeshChannels;
	C_STRUCT aiMeshMorphAnim **mMorphMeshChannels;
};

#endif
