#pragma once

#define H_CHUNK_WIDTH_LOG2 4
#define H_CHUNK_WIDTH (1 << H_CHUNK_WIDTH_LOG2)

#define H_CHUNK_HEIGHT_LOG2 7
#define H_CHUNK_HEIGHT (1 << H_CHUNK_HEIGHT_LOG2)

#define H_MIN_CHUNKS_LOG2 3
#define H_MIN_CHUNKS (1 << H_MIN_CHUNKS_LOG2)
#define H_MAX_CHUNKS_LOG2 6
#define H_MAX_CHUNKS (1 << H_MAX_CHUNKS_LOG2)

#define H_MIN_CHUNKS_IN_CLUSTER 2
#define H_MAX_CHUNKS_IN_CLUSTER 6

#define H_MIN_LONGITUDE (-512)
#define H_MAX_LONGITUDE ( 511)
#define H_MIN_LATITUDE  (-512)
#define H_MAX_LATITUDE  ( 511)


#define H_WORLD_REGION_SIZE_X 24
#define H_WORLD_REGION_SIZE_Y 20


#define H_MAX_WATER_LEVEL_LOG2 14
#define H_MAX_WATER_LEVEL (1 << H_MAX_WATER_LEVEL_LOG2)

#define H_CHUNK_INITIAL_WATER_BLOCK_COUNT 256u


//scale vector for transformation of generated blocks
#define H_BLOCK_SCALE_VECTOR_X 0.2886751345f
#define H_BLOCK_SCALE_VECTOR_Y 0.5f
#define H_BLOCK_SCALE_VECTOR_Z 1.0f


#define H_HEXAGON_EDGE_SIZE 0.5773502691f
#define H_HEXAGON_INNER_RADIUS 0.5f

//space scale vector
#define H_SPACE_SCALE_VECTOR_X 0.8660254037f
#define H_SPACE_SCALE_VECTOR_Y 1.0f
#define H_SPACE_SCALE_VECTOR_Z 1.0f

#define H_PLAYER_HEIGHT 1.75f
#define H_PLAYER_RADIUS (0.25f * 0.9f) // 90% of block side
#define H_PLAYER_EYE_LEVEL 1.67f

#define H_MAX_TEXTURE_SCALE 4

#define H_MAX_SUN_LIGHT 8
#define H_MAX_FIRE_LIGHT 13

// Put new block types at the end of list.
enum h_BlockType : unsigned short
{
	AIR= 0,//MUST BE ZERO
	SPHERICAL_BLOCK,
	STONE,
	SOIL,
	WOOD,
	GRASS,
	WATER,
	SAND,
	FOLIAGE,
	FIRE_STONE,
	BRICK,
	NUM_BLOCK_TYPES,
	BLOCK_UNKNOWN= 65535
};

/* COORDINATE SYSTEM:
  __
 /  \
 \__/

 y
 ^
 |
 |
 |z( up )
 +-------> x

    f
    ______
fl /      \ fr
  /   up   \
  \        /
bl \______/ br
      b

  __    __
 /03\__/23\
 \__/13\__/33\
 /02\__/22\__/
 \__/12\__/32\
 /01\__/21\__/
 \__/11\__/31\
 /00\__/20\__/
 \__/10\__/30\
    \__/  \__/

*/

enum h_Direction:
unsigned char
{
	FORWARD= 0, //y+1
	BACK,       //y-1

	FORWARD_RIGHT= 2, //x+1 y+?
	BACK_LEFT,        //x-1 y-?

	FORWARD_LEFT= 4,  //x-1 y+?
	BACK_RIGHT,       //x+1 y-?

	UP= 6, //z+1
	DOWN,  //z-1

	DIRECTION_UNKNOWN= 255
};

enum h_TransparencyType:
unsigned char
{
	TRANSPARENCY_SOLID=     0, //rock, sand, wood, other blocks with non-alpha textures
	TRANSPARENCY_GLASS=     1, //glass and other syntetic transparent materials
	TRANSPARENCY_GREENERY = 1, //greenery, leafs, grass
	TRANSPARENCY_GAS =      1, //visibly gas ( smoke, plasma, etc )
	TRANSPARENCY_LIQUID=    3, //water, oil, other liquids
	TRANSPARENCY_AIR =      3, //air transparency
};

enum h_WorldMoveDirection:
unsigned char
{
	NORTH,
	SOUTH,
	EAST,
	WEST
};
