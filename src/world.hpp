#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

#include "hex.hpp"
#include "fwd.hpp"
#include "block.hpp"
#include "chunk.hpp"
#include "math_lib/rand.hpp"
#include "math_lib/assert.hpp"
#include "world_action.hpp"
#include "chunk_loader.hpp"
#include "calendar.hpp"

#include "vec.hpp"

/*
Main world logic class.
Methods without comments about thread-safety can be called only in world thread.
*/
class h_World
{
	friend class h_Chunk;

public:
	h_World(
		const h_LongLoadingCallback& long_loading_callback,
		const h_SettingsPtr& settings,
		const h_WorldHeaderPtr& header,
		const char* world_directory );
	~h_World();

	h_Chunk* GetChunk( short X, short Y );//relative chunk coordinates
	const h_Chunk* GetChunk( short X, short Y ) const;//relative chunk coordinates

	//really, returns longitude/2 and latitude/2
	int ChunkCoordToQuadchunkX( int longitude ) const;
	int ChunkCoordToQuadchunkY( int latitude  ) const;

	// Chunk matrix size. Thread safe.
	unsigned int ChunkNumberX() const;
	unsigned int ChunkNumberY() const;

	short Longitude() const;
	short Latitude () const;

	// Add events. Thread safe. Coordinates - global.
	void AddBuildEvent(
		short x, short y, short z,
		h_BlockType block_type,
		h_Direction horizontal_direction, h_Direction vertical_direction );

	void AddDestroyEvent( short x, short y, short z );

	//replace all blocks in radius in this layer( z=const )
	//ACHTUNG! This function unfinished. It ignores destruction of light sources. also, danger of stack owerflow. time ~ 6^radius
	void Blast( short x, short y, short z, short radius );

	// Start main phys loop of world.
	// Call in ui thread.
	// "player" and "renderer" must be valid before StopUpdates call.
	void StartUpdates( h_Player* player, r_IWorldRenderer* renderer );
	void StopUpdates();

	// Pause/unpause world updates
	// Call in ui thread.
	// Must be called, when updates started.
	void PauseUpdates();
	void UnpauseUpdates();

	// Get phys mesh for current player. Method is thread safe.
	// Can return nullptr.
	p_WorldPhysMeshConstPtr GetPhysMesh() const;

	void Save();//save world data to disk

	unsigned char SunLightLevel( short x, short y, short z ) const;
	unsigned char FireLightLevel( short x, short y, short z ) const;
	//returns light level of forward-forwardright upper vertex of prism X16. coordinates - relative
	void GetForwardVertexLight( short x, short y, short z, unsigned char* out_light ) const;
	//returns light level of back-backleft upper vertex of prism X16. coordinates - relative
	void GetBackVertexLight( short x, short y, short z, unsigned char* out_light ) const;

	// Get time, calendar, latitude. All methods thread safe.
	// Time of year, in ticks. 0 - midnight of first year day.
	unsigned int GetTimeOfYear() const;
	const h_Calendar& GetCalendar() const;
	// Latitude of "World" on planet. [-pi; pi]
	float GetGlobalWorldLatitude() const;

	// Current rain intensity. Thread safe.
	float GetRainIntensity() const;

	// Set global coordinates of test mob.
	// THREAD UNSAFE. REMOVE THIS.
	void TestMobSetTargetPosition( int x, int y, int z );
	const m_Vec3& TestMobGetPosition() const;

private:
	//coordinates - relative
	void Build(
		short x, short y, short z,
		h_BlockType block_type,
		h_Direction horizontal_direction, h_Direction vertical_direction );

	void Destroy( short x, short y, short z );
	void FlushActionQueue();

	void RemoveFire( int x, int y, int z );
	void CheckBlockNeighbors( short x, short y, short z );

	void UpdateInRadius( short x, short y, short r );//update chunks in square [x-r;x+r] [y-r;x+r]
	void UpdateWaterInRadius( short x, short y, short r );//update chunks water in square [x-r;x+r] [y-r;x+r]

	void MoveWorld( h_WorldMoveDirection dir );
	void SaveChunk( h_Chunk* ch );
	h_Chunk* LoadChunk( int longitude, int lattude );

	//coordinates of chunks in chunk matrix
	void AddLightToBorderChunk( unsigned int X, unsigned int Y );

	//function uses local coordinates of loaded zone
	void UpdatePhysMesh( short x_min, short x_max, short y_min, short y_max, short z_min, short z_max );

	//clamp coordinates to [ 0; H_CHUNK_WIDTH * chunk_number - 1 ) for x and y
	//and to [ 0; H_CHUNK_HEIGHT - 1 ] for z
	short ClampX( short x ) const;
	short ClampY( short y ) const;
	short ClampZ( short z ) const;
	//safe versions of lighting methods.

	//lighting. relative coordinates
	void SetSunLightLevel( short x, short y, short z, unsigned char l );
	void SetFireLightLevel( short x, short y, short z, unsigned char l );

	void LightWorld();
	//return update radius
	short RelightBlockAdd( short x, short y, short z );
	void RelightBlockRemove( short x, short y, short z );

	void AddSunLight_r( short x, short y, short z, unsigned char l );
	void AddFireLight_r( short x, short y, short z, unsigned char l );
	void AddSunLightSafe_r( short x, short y, short z, unsigned char l );
	void AddFireLightSafe_r( short x, short y, short z, unsigned char l );

	//add light from light sources in cube
	void ShineFireLight( short x_min, short y_min, short z_min, short x_max, short y_max, short z_max );

	void BlastBlock_r( short x, short y, short z, short blast_power );
	bool InBorders( short x, short y, short z ) const;
	bool CanBuild( short x, short y, short z ) const;

	void PhysTick();
	void TestMobTick();

	void RelightWaterModifedChunksLight();//relight chunks, where water was modifed in last ticks
	void WaterPhysTick();
	bool WaterFlow( h_LiquidBlock* from, short to_x, short to_y, short to_z ); //returns true if chunk was midifed
	bool WaterFlowDown( h_LiquidBlock* from, short to_x, short to_y, short to_z );

	void GrassPhysTick();
	void FirePhysTick();

	void RainTick();

	h_Block* NormalBlock( h_BlockType block_type );
	h_GrassBlock* UnactiveGrassBlock();

	void InitNormalBlocks();

private:
	const h_SettingsPtr settings_;
	const h_WorldHeaderPtr header_;

	h_ChunkLoader chunk_loader_;
	std::unique_ptr<g_WorldGenerator> world_generator_;

	// Active area margins. Active area is centred rect of chunks, where world physics works.
	// Outside active area chunks unactive.
	unsigned int active_area_margins_[2];
	// Dimensions of chunks matrix.
	unsigned int chunk_number_x_, chunk_number_y_;
	// Loaded zone beginning longitude and latitude.
	int longitude_, latitude_;

	m_Rand phys_processes_rand_;

	const h_Calendar calendar_;

	r_IWorldRenderer* renderer_= nullptr;
	h_Player* player_= nullptr;

	mutable std::mutex phys_tick_count_mutex_;
	unsigned int phys_tick_count_;

	std::unique_ptr< std::thread > phys_thread_;
	std::atomic<bool> phys_thread_need_stop_;
	std::atomic<bool> phys_thread_paused_;

	//queue 0 - for enqueue, queue 1 - for dequeue
	std::mutex action_queue_mutex_;
	std::queue< h_WorldAction > action_queue_[2];

	mutable std::mutex phys_mesh_mutex_;
	p_WorldPhysMeshConstPtr phys_mesh_;

	struct
	{
		bool is_rain= false;
		unsigned int start_tick;
		unsigned int duration;
		float base_intensity;

		std::atomic<float> current_intensity{ 0.0f };

		const float c_duration_rand_pow= 0.5f;
		m_LongRand rand_generator;
		std::lognormal_distribution<float> duration_rand{ 0.0f, 0.5f };
		std::uniform_real_distribution<float> intensity_rand{ 0.3f, 1.0f };

	} rain_data_;

	int test_mob_discret_pos_[3];
	int test_mob_target_pos_[3];
	unsigned int test_mob_last_think_tick_= 0;
	m_Vec3 test_mob_pos_;

	h_BinaryStorage decompressed_chunk_data_buffer_; // Cache buffer for chunks decompression.

	// All arrays - put at the end of class.

	h_Block normal_blocks_[ size_t(h_BlockType::NumBlockTypes) ];
	h_GrassBlock unactive_grass_block_;

	// Chunks matrix. chunk(x, y)= chunks_[ x + y * H_MAX_CHUNKS ]
	h_Chunk* chunks_[ H_MAX_CHUNKS * H_MAX_CHUNKS ];
};

inline unsigned int h_World::ChunkNumberX() const
{
	return chunk_number_x_;
}

inline unsigned int h_World::ChunkNumberY() const
{
	return chunk_number_y_;
}

inline short h_World::Longitude() const
{
	return longitude_;
}

inline short h_World::Latitude () const
{
	return latitude_;
}

inline h_Chunk* h_World::GetChunk( short X, short Y )
{
	H_ASSERT( X >= 0 && X < (int)chunk_number_x_ );
	H_ASSERT( Y >= 0 && Y < (int)chunk_number_y_ );

	return chunks_[ X | ( Y << H_MAX_CHUNKS_LOG2 ) ];
}

inline const h_Chunk* h_World::GetChunk( short X, short Y ) const
{
	H_ASSERT( X >= 0 && X < (int)chunk_number_x_ );
	H_ASSERT( Y >= 0 && Y < (int)chunk_number_y_ );

	return chunks_[ X | ( Y << H_MAX_CHUNKS_LOG2 ) ];
}

inline h_Block* h_World::NormalBlock( h_BlockType block_type )
{
	return &normal_blocks_[ (int)block_type ];
}

inline h_GrassBlock* h_World::UnactiveGrassBlock()
{
	return &unactive_grass_block_;
}

inline short h_World::ClampX( short x ) const
{
	if( x < 0 )
		return 0;
	short max_x= chunk_number_x_ * H_CHUNK_WIDTH;
	if ( x >= max_x )
		return max_x - 1;
	return x;
}

inline short h_World::ClampY( short y ) const
{
	if( y < 0 )
		return 0;
	short max_y= chunk_number_y_ * H_CHUNK_WIDTH;
	if ( y >= max_y )
		return max_y - 1;
	return y;
}

inline short h_World::ClampZ( short z ) const
{
	if( z < 0 )
		return 0;
	if( z > H_CHUNK_HEIGHT - 1 )
		return H_CHUNK_HEIGHT - 1;
	return z;
}

inline int h_World::ChunkCoordToQuadchunkX( int longitude ) const
{
	return longitude>>1;
}

inline int h_World::ChunkCoordToQuadchunkY( int latitude ) const
{
	return latitude>>1;
}
