#ifndef _MODEL_ASSIMP_H_
#define _MODEL_ASSIMP_H_
#include <stdint.h>
qboolean Assimp_Init(void);
void Assimp_Shutdown(void);

// -------------------------------------------------------------------------------
/** The root structure of the imported data.
 *
 *  Everything that was imported from the given file can be accessed from here.
 *  Objects of this class are generally maintained and owned by Assimp, not
 *  by the caller. You shouldn't want to instance it, nor should you ever try to
 *  delete a given scene on your own.
 */
// -------------------------------------------------------------------------------
#define C_STRUCT struct
#define C_ENUM enum
#define AI_MAX_NUMBER_OF_COLOR_SETS 0x8
#define AI_MAX_NUMBER_OF_TEXTURECOORDS 0x8

// ---------------------------------------------------------------------------
/** Defines how an animation channel behaves outside the defined time
 *  range. This corresponds to aiNodeAnim::mPreState and
 *  aiNodeAnim::mPostState.*/
enum aiAnimBehaviour {
    /** The value from the default node transformation is taken*/
    aiAnimBehaviour_DEFAULT = 0x0,

    /** The nearest key value is used without interpolation */
    aiAnimBehaviour_CONSTANT = 0x1,

    /** The value of the nearest two keys is linearly
     *  extrapolated for the current time value.*/
    aiAnimBehaviour_LINEAR = 0x2,

    /** The animation is repeated.
     *
     *  If the animation key go from n to m and the current
     *  time is t, use the value at (t-n) % (|m-n|).*/
    aiAnimBehaviour_REPEAT = 0x3,

/** This value is not used, it is just here to force the
     *  the compiler to map this enum to a 32 Bit integer  */
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
    //! Index of the vertex which is influenced by the bone.
    unsigned int mVertexId;

    //! The strength of the influence in the range (0...1).
    //! The influence from all bones at one vertex amounts to 1.
    ai_real mWeight;
};

struct aiMatrix4x4 {
    ai_real a1, a2, a3, a4;
    ai_real b1, b2, b3, b4;
    ai_real c1, c2, c3, c4;
    ai_real d1, d2, d3, d4;
};

// -------------------------------------------------------------------------------
/**
 * A node in the imported hierarchy.
 *
 * Each node has name, a parent node (except for the root node),
 * a transformation relative to its parent and possibly several child nodes.
 * Simple file formats don't support hierarchical structures - for these formats
 * the imported scene does consist of only a single root node without children.
 */
// -------------------------------------------------------------------------------
struct aiNode
{
    /** The name of the node.
     *
     * The name might be empty (length of zero) but all nodes which
     * need to be referenced by either bones or animations are named.
     * Multiple nodes may have the same name, except for nodes which are referenced
     * by bones (see #aiBone and #aiMesh::mBones). Their names *must* be unique.
     *
     * Cameras and lights reference a specific node by name - if there
     * are multiple nodes with this name, they are assigned to each of them.
     * <br>
     * There are no limitations with regard to the characters contained in
     * the name string as it is usually taken directly from the source file.
     *
     * Implementations should be able to handle tokens such as whitespace, tabs,
     * line feeds, quotation marks, ampersands etc.
     *
     * Sometimes assimp introduces new nodes not present in the source file
     * into the hierarchy (usually out of necessity because sometimes the
     * source hierarchy format is simply not compatible). Their names are
     * surrounded by @verbatim <> @endverbatim e.g.
     *  @verbatim<DummyRootNode> @endverbatim.
     */
    C_STRUCT aiString mName;

    /** The transformation relative to the node's parent. */
    C_STRUCT aiMatrix4x4 mTransformation;

    /** Parent node. nullptr if this node is the root node. */
    C_STRUCT aiNode* mParent;

    /** The number of child nodes of this node. */
    unsigned int mNumChildren;

    /** The child nodes of this node. nullptr if mNumChildren is 0. */
    C_STRUCT aiNode** mChildren;

    /** The number of meshes of this node. */
    unsigned int mNumMeshes;

    /** The meshes of this node. Each entry is an index into the
      * mesh list of the #aiScene.
      */
    unsigned int* mMeshes;

    /** Metadata associated with this node or nullptr if there is no metadata.
      *  Whether any metadata is generated depends on the source file format. See the
      * @link importer_notes @endlink page for more information on every source file
      * format. Importers that don't document any metadata don't write any.
      */
    C_STRUCT aiMetadata* mMetaData;
};

struct aiBone {
    //! The name of the bone.
    C_STRUCT aiString mName;

    //! The number of vertices affected by this bone.
    //! The maximum value for this member is #AI_MAX_BONE_WEIGHTS.
    unsigned int mNumWeights;

#ifndef ASSIMP_BUILD_NO_ARMATUREPOPULATE_PROCESS
    // The bone armature node - used for skeleton conversion
    // you must enable aiProcess_PopulateArmatureData to populate this
    C_STRUCT aiNode *mArmature;

    // The bone node in the scene - used for skeleton conversion
    // you must enable aiProcess_PopulateArmatureData to populate this
    C_STRUCT aiNode *mNode;

#endif
    //! The influence weights of this bone, by vertex index.
    C_STRUCT aiVertexWeight *mWeights;

    /** Matrix that transforms from bone space to mesh space in bind pose.
     *
     * This matrix describes the position of the mesh
     * in the local space of this bone when the skeleton was bound.
     * Thus it can be used directly to determine a desired vertex position,
     * given the world-space transform of the bone when animated,
     * and the position of the vertex in mesh space.
     *
     * It is sometimes called an inverse-bind matrix,
     * or inverse bind pose matrix.
     */
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

/** This value is not used. It is just here to force the
     *  compiler to map this enum to a 32 Bit integer.
     */
#ifndef SWIG
    _aiPrimitiveType_Force32Bit = INT_MAX
#endif
}; //! enum aiPrimitiveType

struct aiVector3D {
	ai_real x, y, z;
};

// ---------------------------------------------------------------------------
/** A time-value pair specifying a certain 3D vector for the given time. */
struct aiVectorKey {
    /** The time of this key */
    double mTime;

    /** The value of this key */
    C_STRUCT aiVector3D mValue;
};

struct aiQuaternion {
    ai_real w, x, y, z;
};

/** A time-value pair specifying a rotation for the given time.
 *  Rotations are expressed with quaternions. */
struct aiQuatKey {
    /** The time of this key */
    double mTime;

    /** The value of this key */
    C_STRUCT aiQuaternion mValue;
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

// ---------------------------------------------------------------------------
/** Describes the animation of a single node. The name specifies the
 *  bone/node which is affected by this animation channel. The keyframes
 *  are given in three separate series of values, one each for position,
 *  rotation and scaling. The transformation matrix computed from these
 *  values replaces the node's original transformation matrix at a
 *  specific time.
 *  This means all keys are absolute and not relative to the bone default pose.
 *  The order in which the transformations are applied is
 *  - as usual - scaling, rotation, translation.
 *
 *  @note All keys are returned in their correct, chronological order.
 *  Duplicate keys don't pass the validation step. Most likely there
 *  will be no negative time values, but they are not forbidden also ( so
 *  implementations need to cope with them! ) */
struct aiNodeAnim {
    /** The name of the node affected by this animation. The node
     *  must exist and it must be unique.*/
    C_STRUCT aiString mNodeName;

    /** The number of position keys */
    unsigned int mNumPositionKeys;

    /** The position keys of this animation channel. Positions are
     * specified as 3D vector. The array is mNumPositionKeys in size.
     *
     * If there are position keys, there will also be at least one
     * scaling and one rotation key.*/
    C_STRUCT aiVectorKey *mPositionKeys;

    /** The number of rotation keys */
    unsigned int mNumRotationKeys;

    /** The rotation keys of this animation channel. Rotations are
     *  given as quaternions,  which are 4D vectors. The array is
     *  mNumRotationKeys in size.
     *
     * If there are rotation keys, there will also be at least one
     * scaling and one position key. */
    C_STRUCT aiQuatKey *mRotationKeys;

    /** The number of scaling keys */
    unsigned int mNumScalingKeys;

    /** The scaling keys of this animation channel. Scalings are
     *  specified as 3D vector. The array is mNumScalingKeys in size.
     *
     * If there are scaling keys, there will also be at least one
     * position and one rotation key.*/
    C_STRUCT aiVectorKey *mScalingKeys;

    /** Defines how the animation behaves before the first
     *  key is encountered.
     *
     *  The default value is aiAnimBehaviour_DEFAULT (the original
     *  transformation matrix of the affected node is used).*/
    C_ENUM aiAnimBehaviour mPreState;

    /** Defines how the animation behaves after the last
     *  key was processed.
     *
     *  The default value is aiAnimBehaviour_DEFAULT (the original
     *  transformation matrix of the affected node is taken).*/
    C_ENUM aiAnimBehaviour mPostState;
};

// ---------------------------------------------------------------------------
/** An animation consists of key-frame data for a number of nodes. For
 *  each node affected by the animation a separate series of data is given.*/
struct aiAnimation {
    /** The name of the animation. If the modeling package this data was
     *  exported from does support only a single animation channel, this
     *  name is usually empty (length is zero). */
    C_STRUCT aiString mName;

    /** Duration of the animation in ticks.  */
    double mDuration;

    /** Ticks per second. 0 if not specified in the imported file */
    double mTicksPerSecond;

    /** The number of bone animation channels. Each channel affects
     *  a single node. */
    unsigned int mNumChannels;

    /** The node animation channels. Each channel affects a single node.
     *  The array is mNumChannels in size. */
    C_STRUCT aiNodeAnim **mChannels;

    /** The number of mesh animation channels. Each channel affects
     *  a single mesh and defines vertex-based animation. */
    unsigned int mNumMeshChannels;

    /** The mesh animation channels. Each channel affects a single mesh.
     *  The array is mNumMeshChannels in size. */
    C_STRUCT aiMeshAnim **mMeshChannels;

    /** The number of mesh animation channels. Each channel affects
     *  a single mesh and defines morphing animation. */
    unsigned int mNumMorphMeshChannels;

    /** The morph mesh animation channels. Each channel affects a single mesh.
     *  The array is mNumMorphMeshChannels in size. */
    C_STRUCT aiMeshMorphAnim **mMorphMeshChannels;
};

#endif
