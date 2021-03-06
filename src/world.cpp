#include <chrono>
#include <cstring>

#include <zlib.h>

#include "world.hpp"
#include "player.hpp"
#include "math_lib/math.hpp"
#include "renderer/i_world_renderer.hpp"
#include "settings.hpp"
#include "settings_keys.hpp"
#include "world_generator/world_generator.hpp"
#include "world_header.hpp"
#include "world_phys_mesh.hpp"
#include "path_finder.hpp"
#include "console.hpp"
#include "time.hpp"

static constexpr const unsigned int g_updates_frequency= 15;
static constexpr const unsigned int g_update_inrerval_ms= 1000 / g_updates_frequency;
static constexpr const unsigned int g_sleep_interval_on_pause= g_update_inrerval_ms * 4;

static constexpr const unsigned int g_day_duration_ticks= 12 /*min*/ * 60 /*sec*/ * g_updates_frequency;
static constexpr const unsigned int g_days_in_year= 32;
static constexpr const unsigned int g_northern_hemisphere_summer_solstice_day= g_days_in_year / 4;
static constexpr const float g_planet_rotation_axis_inclination= 23.439281f * m_Math::deg2rad;
static constexpr const float g_global_world_latitude= 40.0f * m_Math::deg2rad;

// Returns true, if all is ok.
// Replaces "out_data_compressed" content on success.
// Clears "out_data_compressed" on failure.
static bool CompressChunkData(
	const h_BinaryStorage& data,
	h_BinaryStorage& out_data_compressed )
{
	out_data_compressed.clear();

	const int compress_bound= ::compressBound( data.size() );
	out_data_compressed.resize( sizeof(uint32_t) + compress_bound );
	uLongf result_size= compress_bound;
	const int compress_code=
		::compress(
			out_data_compressed.data() + sizeof(uint32_t), &result_size,
			data.data(), data.size() );
	if( compress_code != Z_OK )
	{
		h_Console::Error( "Can not compress, code: ", compress_code );
		return false;
	}

	out_data_compressed.resize( sizeof(uint32_t) + result_size );

	const uint32_t uncompressed_size= static_cast<uint32_t>(data.size());
	std::memcpy( out_data_compressed.data(), &uncompressed_size, sizeof(uint32_t) );

	return true;
}

// Returns true, if all is ok.
// Replaces "out_data_decompressed" content on success.
// Clears "out_data_decompressed" on failure.
static bool DecompressChunkData(
	const h_BinaryStorage& data_compressed,
	h_BinaryStorage& out_data_decompressed )
{
	out_data_decompressed.clear();

	uint32_t uncompressed_size;
	std::memcpy( &uncompressed_size, data_compressed.data(), sizeof(uint32_t) );

	out_data_decompressed.resize( uncompressed_size );

	uLongf uncompressed_size_returned= uncompressed_size;
	const int uncompress_code=
		::uncompress(
			out_data_decompressed.data(), &uncompressed_size_returned,
			data_compressed.data() + sizeof(uint32_t), data_compressed.size() - sizeof(uint32_t) );
	if( uncompress_code != Z_OK )
	{
		h_Console::Error( "Can not uncompress, code: ", uncompress_code );
		return false;
	}
	if( uncompressed_size_returned != uncompressed_size )
	{
		h_Console::Error( "Can not uncompress - bad size" );
		return false;
	}

	return true;
}

// day of spring equinox
// some time after sunrise.
static constexpr const unsigned int g_world_start_tick=
	( g_days_in_year + g_northern_hemisphere_summer_solstice_day - g_days_in_year / 4 ) %
	g_days_in_year *
	g_day_duration_ticks +
	g_day_duration_ticks / 4 + g_day_duration_ticks / 16;

h_World::h_World(
	const h_LongLoadingCallback& long_loading_callback,
	const h_SettingsPtr& settings,
	const h_WorldHeaderPtr& header,
	const char* world_directory )
	: settings_( settings )
	, header_( header )
	, chunk_loader_( world_directory )
	, calendar_(
		g_day_duration_ticks,
		g_days_in_year,
		g_planet_rotation_axis_inclination,
		g_northern_hemisphere_summer_solstice_day )
	, phys_tick_count_( header->ticks != 0 ? header->ticks : g_world_start_tick )
	, phys_thread_need_stop_(false)
	, phys_thread_paused_(false)
	, unactive_grass_block_( 0, 0, 1, false )
{
	const float c_initial_progress= 0.05f;
	const float c_progress_for_generation= 0.2f;
	const float c_progres_per_chunk= 0.01f;
	const float c_lighting_progress= 0.2f;

	rain_data_.is_rain= header_->rain_data.is_rain;
	rain_data_.start_tick= header_->rain_data.start_tick;
	rain_data_.duration= header_->rain_data.duration;
	mLongRandSetState( rain_data_.rand_generator, header_->rain_data.rand_state );
	rain_data_.base_intensity= header->rain_data.base_intensity;

	InitNormalBlocks();

	chunk_number_x_= std::max( std::min( settings_->GetInt( h_SettingsKeys::chunk_number_x, 14 ), H_MAX_CHUNKS ), H_MIN_CHUNKS );
	chunk_number_y_= std::max( std::min( settings_->GetInt( h_SettingsKeys::chunk_number_y, 12 ), H_MAX_CHUNKS ), H_MIN_CHUNKS );

	// Active area margins. Minimal active area have size 5.
	active_area_margins_[0]=
		std::max(
			2,
			std::min(
				settings_->GetInt( h_SettingsKeys::active_area_margins_x, 2 ),
				int(chunk_number_x_ / 2 - 2) ) );
	active_area_margins_[1]=
		std::max(
			2,
			std::min(
				settings_->GetInt( h_SettingsKeys::active_area_margins_y, 2 ),
				int(chunk_number_y_ / 2 - 2) ) );

	settings_->SetSetting( h_SettingsKeys::chunk_number_x, (int)chunk_number_x_ );
	settings_->SetSetting( h_SettingsKeys::chunk_number_y, (int)chunk_number_y_ );
	settings_->SetSetting( h_SettingsKeys::active_area_margins_x, (int)active_area_margins_[0] );
	settings_->SetSetting( h_SettingsKeys::active_area_margins_y, (int)active_area_margins_[1] );

	{ // Move world to player position
		int player_xy[2];
		pGetHexogonCoord( m_Vec2(header_->player.x, header->player.y), &player_xy[0], &player_xy[1] );
		int player_longitude= ( player_xy[0] + (H_CHUNK_WIDTH >> 1) ) >> H_CHUNK_WIDTH_LOG2;
		int player_latitude = ( player_xy[1] + (H_CHUNK_WIDTH >> 1) ) >> H_CHUNK_WIDTH_LOG2;

		longitude_= player_longitude - chunk_number_x_/2;
		latitude_ = player_latitude  - chunk_number_y_/2;
	}

	float progress_scaler= 1.0f / (
		c_initial_progress + c_progress_for_generation +
		c_progres_per_chunk * float( chunk_number_x_ * chunk_number_y_ ) +
		c_lighting_progress );
	float progress= 0.0f;

	long_loading_callback( progress+= c_initial_progress * progress_scaler );

	g_WorldGenerationParameters parameters;
	parameters.world_dir= world_directory;
	parameters.size[0]= parameters.size[1]= 512;
	parameters.cell_size_log2= 0;
	parameters.seed= 24;

	world_generator_.reset( new g_WorldGenerator( parameters ) );
	world_generator_->Generate();
	//world_generator_->DumpDebugResult();

	long_loading_callback( progress+= c_progress_for_generation * progress_scaler );

	for( unsigned int i= 0; i< chunk_number_x_; i++ )
	for( unsigned int j= 0; j< chunk_number_y_; j++ )
	{
		chunks_[ i + j * H_MAX_CHUNKS ]= LoadChunk( i+longitude_, j+latitude_);

		long_loading_callback( progress+= c_progres_per_chunk * progress_scaler );
	}

	LightWorld();

	long_loading_callback( progress+= c_lighting_progress * progress_scaler );

	test_mob_target_pos_[0]= test_mob_discret_pos_[0]= 0;
	test_mob_target_pos_[1]= test_mob_discret_pos_[1]= 0;
	test_mob_target_pos_[2]= test_mob_discret_pos_[2]= 72;
}

h_World::~h_World()
{
	header_->ticks= phys_tick_count_;

	header_->rain_data.is_rain= rain_data_.is_rain;
	header_->rain_data.start_tick= rain_data_.start_tick;
	header_->rain_data.duration= rain_data_.duration;
	header_->rain_data.rand_state= mLongRandGetState( rain_data_.rand_generator );
	header_->rain_data.base_intensity= rain_data_.base_intensity;

	H_ASSERT(!phys_thread_);

	for( unsigned int x= 0; x< chunk_number_x_; x++ )
		for( unsigned int y= 0; y< chunk_number_y_; y++ )
		{
			h_Chunk* ch= GetChunk(x,y);
			SaveChunk( ch );
			chunk_loader_.FreeChunkData( ch->Longitude(), ch->Latitude() );
			delete ch;
		}
}

void h_World::AddBuildEvent(
	const int x, const int y, const int z,
	h_BlockType block_type,
	h_Direction horizontal_direction, h_Direction vertical_direction )
{
	h_WorldAction act;

	act.type= h_WorldAction::Type::Build;
	act.block_type= block_type;
	act.horizontal_direction= horizontal_direction;
	act.vertical_direction= vertical_direction;
	act.coord[0]= x;
	act.coord[1]= y;
	act.coord[2]= z;

	std::lock_guard< std::mutex > lock(action_queue_mutex_);
	action_queue_[0].push( act );
}

void h_World::AddDestroyEvent( const int x, const int y, const int z )
{
	h_WorldAction act;
	act.type= h_WorldAction::Type::Destroy;
	act.coord[0]= x;
	act.coord[1]= y;
	act.coord[2]= z;

	std::lock_guard< std::mutex > lock(action_queue_mutex_);
	action_queue_[0].push( act );
}

void h_World::Blast( int x, int y, int z, int radius )
{
	if( !InBorders( x, y, z ) )
		return;

	for( int k= z, r=radius; k< z+radius; k++, r-- )
		BlastBlock_r( x, y, k, r );
	for( int k= z-1, r=radius-1; k> z-radius; k--, r-- )
		BlastBlock_r( x, y, k, r );

	for( int i= x - radius; i< x+radius; i++ )
		for( int j= y - radius; j< y+radius; j++ )
			for( int k= z - radius; k< z+radius; k++ )
				RelightBlockRemove( i, j, k );

	UpdateInRadius( x, y, radius );
}

void h_World::StartUpdates( h_Player* player, r_IWorldRenderer* renderer )
{
	H_ASSERT( player );
	H_ASSERT( renderer );
	H_ASSERT( !player_ );
	H_ASSERT( !renderer_ );
	H_ASSERT( !phys_thread_ );

	player_= player;
	renderer_= renderer;

	phys_thread_need_stop_.store(false);
	phys_thread_paused_.store(false);
	phys_thread_.reset( new std::thread( &h_World::PhysTick, this ) );

	h_Console::Info( "World updates started" );
}

void h_World::StopUpdates()
{
	H_ASSERT( phys_thread_ );

	H_ASSERT( player_ );
	H_ASSERT( renderer_ );

	phys_thread_need_stop_.store(true);
	phys_thread_paused_.store(false);
	phys_thread_->join();
	phys_thread_.reset();

	player_= nullptr;
	renderer_= nullptr;

	h_Console::Info( "World updates stopped" );
}

void h_World::PauseUpdates()
{
	H_ASSERT( phys_thread_ );

	phys_thread_paused_.store(true);
}

void h_World::UnpauseUpdates()
{
	H_ASSERT( phys_thread_ );

	phys_thread_paused_.store(false);
}

p_WorldPhysMeshConstPtr h_World::GetPhysMesh() const
{
	std::lock_guard<std::mutex> lock( phys_mesh_mutex_ );
	return phys_mesh_;
}

void h_World::Save()
{
	for( unsigned int x= 0; x< chunk_number_x_; x++ )
		for( unsigned int y= 0; y< chunk_number_y_; y++ )
			SaveChunk( GetChunk(x,y) );
	chunk_loader_.ForceSaveAllChunks();
}

unsigned int h_World::GetTimeOfYear() const
{
	std::lock_guard<std::mutex> lock(phys_tick_count_mutex_);

	return phys_tick_count_ % ( g_day_duration_ticks * g_days_in_year );
}

const h_Calendar& h_World::GetCalendar() const
{
	return calendar_;
}

float h_World::GetGlobalWorldLatitude() const
{
	return g_global_world_latitude;
}

float h_World::GetRainIntensity() const
{
	return rain_data_.current_intensity.load();
}

void h_World::TestMobSetTargetPosition( int x, int y, int z )
{
	test_mob_target_pos_[0]= x;
	test_mob_target_pos_[1]= y;
	test_mob_target_pos_[2]= z;
}

const m_Vec3& h_World::TestMobGetPosition() const
{
	return test_mob_pos_;
}

void h_World::Build(
	int x, int y, int z,
	h_BlockType block_type,
	h_Direction horizontal_direction, h_Direction vertical_direction )
{
	if( !InBorders( x, y, z ) )
		return;
	if( !CanBuild( x, y, z ) )
		return;

	int local_x= x & (H_CHUNK_WIDTH - 1);
	int local_y= y & (H_CHUNK_WIDTH - 1);

	int chunk_x= x >> H_CHUNK_WIDTH_LOG2;
	int chunk_y= y >> H_CHUNK_WIDTH_LOG2;

	if( block_type == h_BlockType::Water )
	{
		h_LiquidBlock* b;
		h_Chunk* ch= GetChunk( chunk_x, chunk_y );
		ch-> SetBlock(
			local_x, local_y, z,
			b= ch->NewWaterBlock() );

		b->x_= local_x;
		b->y_= local_y;
		b->z_= z;
	}
	else if( block_type == h_BlockType::FireStone )
	{
		h_Chunk* ch= GetChunk( chunk_x, chunk_y );
		h_LightSource* s= ch->NewLightSource( local_x, local_y, z, h_BlockType::FireStone );
		s->SetLightLevel( H_MAX_FIRE_LIGHT );

		ch->SetBlock( local_x, local_y, z, s );
		AddFireLight_r( x, y, z, H_MAX_FIRE_LIGHT );
	}
	else if( block_type == h_BlockType::Fire )
	{
		h_Chunk* ch= GetChunk( chunk_x, chunk_y );

		h_Fire* fire= new h_Fire();
		fire->x_= local_x;
		fire->y_= local_y;
		fire->z_= z;

		ch->light_source_list_.push_back( fire );
		ch->fire_list_.push_back( fire );

		ch->SetBlock( local_x, local_y, z, fire );
		AddFireLight_r( x, y, z, fire->LightLevel() );
	}
	else if( block_type == h_BlockType::Grass )
	{
		h_Chunk* ch= GetChunk( chunk_x, chunk_y );
		h_GrassBlock* grass_block= ch->NewActiveGrassBlock( local_x, local_y, z );
		ch->SetBlock( local_x, local_y, z, grass_block );
	}
	else
	{
		h_BlockForm form= h_Block::Form( block_type );
		if( form == h_BlockForm::Plate || form == h_BlockForm::Bisected )
		{
			h_Chunk* ch= GetChunk( chunk_x, chunk_y );

			h_Direction direction= form == h_BlockForm::Plate ? vertical_direction : horizontal_direction;

			h_NonstandardFormBlock* block=
				GetChunk( chunk_x, chunk_y )->
				NewNonstandardFormBlock(
					local_x, local_y, z,
					block_type, direction );

			ch->SetBlock( local_x, local_y, z, block );
		}
		else
		{
			GetChunk( chunk_x, chunk_y )->
			SetBlock(
				local_x, local_y, z,
				NormalBlock( block_type ) );
		}
	}

	int r= 1;
	if( block_type != h_BlockType::Water )
		r= RelightBlockAdd( x, y, z ) + 1;

	UpdateInRadius( x, y, r );
	UpdateWaterInRadius( x, y, r );

	CheckBlockNeighbors( x, y, z );
}

void h_World::Destroy( const int x, const int y, const int z )
{
	if( !InBorders( x, y, z ) )
		return;

	int local_x= x & (H_CHUNK_WIDTH - 1);
	int local_y= y & (H_CHUNK_WIDTH - 1);

	int chunk_x= x >> H_CHUNK_WIDTH_LOG2;
	int chunk_y= y >> H_CHUNK_WIDTH_LOG2;

	h_Chunk* ch= GetChunk( chunk_x, chunk_y );
	h_Block* block= ch->GetBlock( local_x, local_y, z );
	if( block->Type() == h_BlockType::Water )
	{

	}
	else if( block->Type() == h_BlockType::FireStone )
	{
		ch->DeleteLightSource( local_x, local_y, z );
		ch->SetBlock(
			local_x, local_y, z,
			NormalBlock( h_BlockType::Air ) );

		RelightBlockAdd( x, y, z );

		RelightBlockRemove( x, y, z );
		UpdateInRadius( x, y, H_MAX_FIRE_LIGHT );
		UpdateWaterInRadius( x, y, H_MAX_FIRE_LIGHT );
	}
	else if( block->Type() == h_BlockType::Grass )
	{
		// Delete grass block from list of active grass blocks, if it is active.
		h_GrassBlock* grass_block= static_cast<h_GrassBlock*>(block);
		if( grass_block->IsActive() )
		{
			bool deleted= false;
			for( unsigned int i= 0; i < ch->active_grass_blocks_.size(); i++ )
			{
				if( ch->active_grass_blocks_[i] == grass_block )
				{
					ch->active_grass_blocks_allocator_.Delete( grass_block );

					if( i != ch->active_grass_blocks_.size() - 1 )
						ch->active_grass_blocks_[i]= ch->active_grass_blocks_.back();

					ch->active_grass_blocks_.pop_back();

					deleted= true;
					break;
				}
			}

			(void)deleted;
			H_ASSERT(deleted);
		}

		ch->SetBlock(
			local_x, local_y, z,
			NormalBlock( h_BlockType::Air ) );

		RelightBlockRemove( x, y, z );
		UpdateInRadius( x, y, H_MAX_FIRE_LIGHT );
		UpdateWaterInRadius( x, y, H_MAX_FIRE_LIGHT );
	}
	else if( h_Block::Form( block->Type() ) != h_BlockForm::Full )
	{
		auto nonstandard_form_block= static_cast<h_NonstandardFormBlock*>(block);

		bool deleted= false;
		for( unsigned int i= 0; i < ch->nonstandard_form_blocks_.size(); i++ )
		{
			if( ch->nonstandard_form_blocks_[i] == nonstandard_form_block )
			{
				ch->nonstandard_form_blocks_allocator_.Delete( nonstandard_form_block );

				if( i != ch->nonstandard_form_blocks_.size() - 1 )
					ch->nonstandard_form_blocks_[i]= ch->nonstandard_form_blocks_.back();

				ch->nonstandard_form_blocks_.pop_back();

				deleted= true;
				break;
			}
		}

		(void)deleted;
		H_ASSERT(deleted);

		ch->SetBlock(
			local_x, local_y, z,
			NormalBlock( h_BlockType::Air ) );

		RelightBlockRemove( x, y, z );
		UpdateInRadius( x, y, H_MAX_FIRE_LIGHT );
		UpdateWaterInRadius( x, y, H_MAX_FIRE_LIGHT );
	}
	else
	{
		ch->SetBlock(
			local_x, local_y, z,
			NormalBlock( h_BlockType::Air ) );

		RelightBlockRemove( x, y, z );
		UpdateInRadius( x, y, H_MAX_FIRE_LIGHT );
		UpdateWaterInRadius( x, y, H_MAX_FIRE_LIGHT );
	}

	CheckBlockNeighbors( x, y, z );
}

void h_World::FlushActionQueue()
{
	action_queue_mutex_.lock();
	action_queue_[0].swap( action_queue_[1] );
	action_queue_mutex_.unlock();

	while( action_queue_[1].size() != 0 )
	{
		h_WorldAction act= action_queue_[1].front();
		action_queue_[1].pop();

		// global coordinates to local
		act.coord[0]-= longitude_ << H_CHUNK_WIDTH_LOG2;
		act.coord[1]-= latitude_  << H_CHUNK_WIDTH_LOG2;

		switch( act.type )
		{
		case h_WorldAction::Type::Build:
			Build(
				act.coord[0], act.coord[1], act.coord[2],
				act.block_type,
				act.horizontal_direction, act.vertical_direction );
			break;

		case h_WorldAction::Type::Destroy:
			Destroy( act.coord[0], act.coord[1], act.coord[2] );
			break;
		};
	}
}

void h_World::RemoveFire( const int x, const int y, const int z )
{
	h_Chunk* chunk= GetChunk( x >> H_CHUNK_WIDTH_LOG2, y >> H_CHUNK_WIDTH_LOG2 );

	const int local_x= x & (H_CHUNK_WIDTH - 1);
	const int local_y= y & (H_CHUNK_WIDTH - 1);
	const int addr= BlockAddr( local_x, local_y, z );

	h_Block* block= chunk->GetBlock( addr );
	H_ASSERT( block->Type() == h_BlockType::Fire );

	h_Fire* fire= static_cast<h_Fire*>( block );

	chunk->DeleteLightSource( fire );
	chunk->SetBlock( addr, NormalBlock( h_BlockType::Air ) );

	const int r= chunk->FireLightLevel( addr );
	RelightBlockAdd( x, y, z );
	RelightBlockRemove( x, y, z );

	UpdateInRadius( x, y, r );
	UpdateWaterInRadius( x, y, r );

	for( unsigned int i= 0; i < chunk->fire_list_.size(); i++ )
	{
		if( chunk->fire_list_[i] == fire )
		{
			if( i + 1 != chunk->fire_list_.size() )
				chunk->fire_list_[i]= chunk->fire_list_.back();
			chunk->fire_list_.pop_back();

			return;
		}
	}
	H_ASSERT(false);
}

void h_World::CheckBlockNeighbors( const int x, const int y, const int z )
{
	int forward_side_y= y + ( (x^1) & 1 );
	int back_side_y= y - (x & 1);

	int neighbors[7][2]=
	{
		{ x, y },
		{ x, y + 1 },
		{ x, y - 1 },
		{ x + 1, forward_side_y },
		{ x + 1, back_side_y },
		{ x - 1, forward_side_y },
		{ x - 1, back_side_y },
	};

	for( unsigned int n= 0; n < 7; n++ )
	{
		const int chunk_x= neighbors[n][0] >> H_CHUNK_WIDTH_LOG2;
		const int chunk_y= neighbors[n][1] >> H_CHUNK_WIDTH_LOG2;

		h_Chunk* chunk= GetChunk( chunk_x, chunk_y );

		const int local_x= neighbors[n][0] & (H_CHUNK_WIDTH-1);
		const int local_y= neighbors[n][1] & (H_CHUNK_WIDTH-1);
		const int neighbor_addr= BlockAddr( local_x, local_y, 0 );

		for( int neighbor_z= std::max(0, int(z) - 2);
			 neighbor_z <= std::min(int(z) + 1, H_CHUNK_HEIGHT - 1);
			 neighbor_z++ )
		{
			h_Block* block= chunk->blocks_[ neighbor_addr + neighbor_z ];
			switch( block->Type() )
			{
				// Activate unactive grass blocks.
				case h_BlockType::Grass:
				{
					h_GrassBlock* grass_block= static_cast<h_GrassBlock*>(block);
					if( !grass_block->IsActive() )
						chunk->blocks_[ neighbor_addr + neighbor_z ]=
							chunk->NewActiveGrassBlock( local_x, local_y, neighbor_z );
				}
				break;

				// If there is air under sand block - sand must fail.
				case h_BlockType::Sand:
				{
					h_BlockType lower_block_type= chunk->blocks_[ neighbor_addr + neighbor_z - 1 ]->Type();
					if( lower_block_type == h_BlockType::Air || lower_block_type == h_BlockType::Water  ||
						lower_block_type == h_BlockType::Fire )
					{
						h_FailingBlock* failing_block= chunk->failing_blocks_alocatior_.New( block, local_x, local_y, neighbor_z );
						chunk->failing_blocks_.push_back( failing_block );
						chunk->SetBlock( local_x, local_y, neighbor_z, failing_block );

						RelightBlockRemove( neighbors[n][0], neighbors[n][1], neighbor_z );
						UpdateInRadius( neighbors[n][0], neighbors[n][1], H_MAX_FIRE_LIGHT );
						UpdateWaterInRadius( neighbors[n][0], neighbors[n][1], H_MAX_FIRE_LIGHT );
					}
				}
				break;

			// If something happens near water blocks, water mesh must be rebuilded,
			// bacause it depends on nonwater blocks.
			case h_BlockType::Water:
				renderer_->UpdateChunkWater( chunk_x, chunk_y );
				break;

				default: break;
			};
		} // for z
	} // for xy neighbors
}

void h_World::UpdateInRadius( const int x, const int y, const int r )
{
	int x_min, x_max, y_min, y_max;
	x_min= ClampX( x - r );
	x_max= ClampX( x + r );
	y_min= ClampY( y - r );
	y_max= ClampY( y + r );

	x_min>>= H_CHUNK_WIDTH_LOG2;
	x_max>>= H_CHUNK_WIDTH_LOG2;
	y_min>>= H_CHUNK_WIDTH_LOG2;
	y_max>>= H_CHUNK_WIDTH_LOG2;
	for( int i= x_min; i<= x_max; i++ )
	for( int j= y_min; j<= y_max; j++ )
		renderer_->UpdateChunk( i, j );
}

void h_World::UpdateWaterInRadius( const int x, const int y, const int r )
{
	int x_min, x_max, y_min, y_max;
	x_min= ClampX( x - r );
	x_max= ClampX( x + r );
	y_min= ClampY( y - r );
	y_max= ClampY( y + r );

	x_min>>= H_CHUNK_WIDTH_LOG2;
	x_max>>= H_CHUNK_WIDTH_LOG2;
	y_min>>= H_CHUNK_WIDTH_LOG2;
	y_max>>= H_CHUNK_WIDTH_LOG2;
	for( int i= x_min; i<= x_max; i++ )
	for( int j= y_min; j<= y_max; j++ )
		renderer_->UpdateChunkWater( i, j );
}

void h_World::MoveWorld( h_WorldMoveDirection dir )
{
	unsigned int i, j;
	switch ( dir )
	{
	case NORTH:
		for( i= 0; i< chunk_number_x_; i++ )
		{
			h_Chunk* deleted_chunk= chunks_[ i | ( 0 << H_MAX_CHUNKS_LOG2 ) ];
			SaveChunk( deleted_chunk );
			chunk_loader_.FreeChunkData( deleted_chunk->Longitude(), deleted_chunk->Latitude() );
			delete deleted_chunk;
			for( j= 1; j< chunk_number_y_; j++ )
			{
				chunks_[ i | ( (j-1) << H_MAX_CHUNKS_LOG2 ) ]=
					chunks_[ i | ( j << H_MAX_CHUNKS_LOG2 ) ];
			}

			chunks_[ i | ( (chunk_number_y_-1) << H_MAX_CHUNKS_LOG2 ) ]=
				LoadChunk( i + longitude_, chunk_number_y_ + latitude_ );
		}
		for( i= 0; i< chunk_number_x_; i++ )
			AddLightToBorderChunk( i, chunk_number_y_ - 1 );
		latitude_++;

		break;

	case SOUTH:
		for( i= 0; i< chunk_number_x_; i++ )
		{
			h_Chunk* deleted_chunk= chunks_[ i | ( (chunk_number_y_-1) << H_MAX_CHUNKS_LOG2 ) ];
			SaveChunk( deleted_chunk );
			chunk_loader_.FreeChunkData( deleted_chunk->Longitude(), deleted_chunk->Latitude() );
			delete deleted_chunk;
			for( j= chunk_number_y_-1; j> 0; j-- )
			{
				chunks_[ i | ( j << H_MAX_CHUNKS_LOG2 ) ]=
					chunks_[ i | ( (j-1) << H_MAX_CHUNKS_LOG2 ) ];
			}

			chunks_[ i | ( 0 << H_MAX_CHUNKS_LOG2 ) ]=
				LoadChunk( i + longitude_,  latitude_-1 );
		}
		for( i= 0; i< chunk_number_x_; i++ )
			AddLightToBorderChunk( i, 0 );
		latitude_--;

		break;

	case EAST:
		for( j= 0; j< chunk_number_y_; j++ )
		{
			h_Chunk* deleted_chunk= chunks_[ 0 | ( j << H_MAX_CHUNKS_LOG2 ) ];
			SaveChunk( deleted_chunk );
			chunk_loader_.FreeChunkData( deleted_chunk->Longitude(), deleted_chunk->Latitude() );
			delete deleted_chunk;
			for( i= 1; i< chunk_number_x_; i++ )
			{
				chunks_[ (i-1) | ( j << H_MAX_CHUNKS_LOG2 ) ]=
					chunks_[ i | ( j << H_MAX_CHUNKS_LOG2 ) ];
			}
			chunks_[ ( chunk_number_x_-1) | ( j << H_MAX_CHUNKS_LOG2 ) ]=
				LoadChunk( longitude_+chunk_number_x_, latitude_ + j );
		}
		for( j= 0; j< chunk_number_y_; j++ )
			AddLightToBorderChunk( chunk_number_x_-1, j );
		longitude_++;

		break;

	case WEST:
		for( j= 0; j< chunk_number_y_; j++ )
		{
			h_Chunk* deleted_chunk= chunks_[ ( chunk_number_x_-1) | ( j << H_MAX_CHUNKS_LOG2 ) ];
			SaveChunk( deleted_chunk );
			chunk_loader_.FreeChunkData( deleted_chunk->Longitude(), deleted_chunk->Latitude() );
			delete deleted_chunk;
			for( i= chunk_number_x_-1; i> 0; i-- )
			{
				chunks_[ i | ( j << H_MAX_CHUNKS_LOG2 ) ]=
					chunks_[ (i-1) | ( j << H_MAX_CHUNKS_LOG2 ) ];
			}
			chunks_[ 0 | ( j << H_MAX_CHUNKS_LOG2 ) ]=
				LoadChunk( longitude_-1, latitude_ + j );
		}
		for( j= 0; j< chunk_number_y_; j++ )
			AddLightToBorderChunk( 0, j );
		longitude_--;

		break;
	};

	// Mark for renderer near-border chunks as updated.
	renderer_->UpdateWorldPosition( longitude_, latitude_ );

	switch( dir )
	{
	case NORTH:
		for( i= 0; i< chunk_number_x_; i++ )
		{
			renderer_->UpdateChunk( i, chunk_number_y_ - 2, true );
			renderer_->UpdateChunkWater( i, chunk_number_y_ - 2, true );
		}
		break;

	case SOUTH:
		for( i= 0; i< chunk_number_x_; i++ )
		{
			renderer_->UpdateChunk( i, 1, true );
			renderer_->UpdateChunkWater( i, 1, true );
		}
		break;

	case EAST:
		for( j= 0; j< chunk_number_y_; j++ )
		{
			renderer_->UpdateChunk( chunk_number_x_ - 2, j, true );
			renderer_->UpdateChunkWater( chunk_number_x_ - 2, j, true );
		}
		break;

	case WEST:
		for( j= 0; j< chunk_number_y_; j++ )
		{
			renderer_->UpdateChunk( 1, j, true );
			renderer_->UpdateChunkWater( 1, j, true );
		}
		break;
	};
}

void h_World::SaveChunk( h_Chunk* ch )
{
	h_BinaryStorage data_uncompressed;
	h_BinaryOuptutStream stream( data_uncompressed );

	HEXCHUNK_header header;

	header.water_block_count= ch->GetWaterList().size();
	header.longitude= ch->Longitude();
	header.latitude= ch->Latitude();

	header.Write( stream );
	ch->SaveChunkToFile( stream );

	CompressChunkData(
		data_uncompressed,
		chunk_loader_.GetChunkData( ch->Longitude(), ch->Latitude() ) );
}

h_Chunk* h_World::LoadChunk( int lon, int lat )
{
	const h_BinaryStorage& chunk_data_compressed= chunk_loader_.GetChunkData( lon, lat );
	if( chunk_data_compressed.empty() )
		return new h_Chunk( this, lon, lat, world_generator_.get() );

	if( !DecompressChunkData( chunk_data_compressed, decompressed_chunk_data_buffer_ ) )
		return new h_Chunk( this, lon, lat, world_generator_.get() );

	h_BinaryInputStream stream( decompressed_chunk_data_buffer_ );

	HEXCHUNK_header header;
	header.Read( stream );

	return new h_Chunk( this, header, stream );
}

void h_World::UpdatePhysMesh( int x_min, int x_max, int y_min, int y_max, int z_min, int z_max )
{
	p_WorldPhysMesh phys_mesh;

	int X= Longitude() * H_CHUNK_WIDTH;
	int Y= Latitude () * H_CHUNK_WIDTH;

	x_min= std::max( int(2), x_min );
	y_min= std::max( int(2), y_min );
	z_min= std::max( int(0), z_min );
	x_max= std::min( x_max, int( chunk_number_x_ * H_CHUNK_WIDTH - 2 ) );
	y_max= std::min( y_max, int( chunk_number_y_ * H_CHUNK_WIDTH - 2 ) );
	z_max= std::min( z_max, int( H_CHUNK_HEIGHT - 1 ) );

	for( int x= x_min; x< x_max; x++ )
	for( int y= y_min; y< y_max; y++ )
	{
		const unsigned char *t_p, *t_f_p, *t_fr_p, *t_br_p;
		int x1, y1;

		t_p=
			GetChunk( x >> H_CHUNK_WIDTH_LOG2, y >> H_CHUNK_WIDTH_LOG2 )->GetTransparencyData() +
			BlockAddr( x&(H_CHUNK_WIDTH-1), y&(H_CHUNK_WIDTH-1), 0 );

		y1= y + 1;
		t_f_p=
			GetChunk( x >> H_CHUNK_WIDTH_LOG2, y1 >> H_CHUNK_WIDTH_LOG2 )->GetTransparencyData() +
			BlockAddr( x&(H_CHUNK_WIDTH-1), y1&(H_CHUNK_WIDTH-1), 0 );

		x1= x+1;
		y1= y + ( 1&(x+1) );
		t_fr_p=
			GetChunk( x1 >> H_CHUNK_WIDTH_LOG2, y1 >> H_CHUNK_WIDTH_LOG2 )->GetTransparencyData() +
			BlockAddr( x1&(H_CHUNK_WIDTH-1), y1&(H_CHUNK_WIDTH-1), 0 );

		x1= x+1;
		y1= y - (x&1);
		t_br_p=
			GetChunk( x1 >> H_CHUNK_WIDTH_LOG2, y1 >> H_CHUNK_WIDTH_LOG2 )->GetTransparencyData() +
			BlockAddr( x1&(H_CHUNK_WIDTH-1), y1&(H_CHUNK_WIDTH-1), 0 );

		for( int z= z_min; z < z_max; z++ )
		{
			unsigned char t, t_up, t_f, t_fr, t_br;
			t= t_p[z] & H_VISIBLY_TRANSPARENCY_BITS;
			t_up= t_p[z+1] & H_VISIBLY_TRANSPARENCY_BITS;
			t_f= t_f_p[z] & H_VISIBLY_TRANSPARENCY_BITS;
			t_fr= t_fr_p[z] & H_VISIBLY_TRANSPARENCY_BITS;
			t_br= t_br_p[z] & H_VISIBLY_TRANSPARENCY_BITS;

			if( t != t_up )
			{
				phys_mesh.upper_block_faces.emplace_back( x + X, y + Y, z + 1, t > t_up ? h_Direction::Down : h_Direction::Up );
			}
			if( t != t_fr )
			{
				if( t > t_fr )
					phys_mesh.block_sides.emplace_back( x + X + 1, y + Y + ((x+1)&1), z, h_Direction::BackLeft );
				else
					phys_mesh.block_sides.emplace_back( x + X, y + Y, z, h_Direction::ForwardRight );
			}
			if( t != t_br )
			{
				if( t > t_br )
					phys_mesh.block_sides.emplace_back( x + X + 1, y + Y - (x&1), z, h_Direction::ForwardLeft );
				else
					phys_mesh.block_sides.emplace_back( x + X, y + Y, z, h_Direction::BackRight );
			}
			if( t!= t_f )
			{
				if( t > t_f )
					phys_mesh.block_sides.emplace_back( x + X, y + Y + 1, z, h_Direction::Back );
				else
					phys_mesh.block_sides.emplace_back( x + X, y + Y, z, h_Direction::Forward );
			}
		} // for z
	} // for xy

	for( int x= x_min; x< x_max; x++ )
	for( int y= y_min; y< y_max; y++ )
	{
		h_Chunk* chunk= GetChunk( x >> H_CHUNK_WIDTH_LOG2, y >> H_CHUNK_WIDTH_LOG2 );
		const h_Block* const* blocks=
			chunk->GetBlocksData() +
			BlockAddr( x&(H_CHUNK_WIDTH-1), y&(H_CHUNK_WIDTH-1), 0 );

		for( int z= z_min; z < z_max; z++ )
		{
			if( blocks[z]->Type() == h_BlockType::Water )
			{
				const h_LiquidBlock* water_block= static_cast<const h_LiquidBlock*>( blocks[z] );

				p_WaterBlock water_phys_block;
				water_phys_block.x= x + X;
				water_phys_block.y= y + Y;
				water_phys_block.z= z;
				water_phys_block.water_level= float(water_block->LiquidLevel()) / float(H_MAX_WATER_LEVEL);

				phys_mesh.water_blocks.push_back( water_phys_block );
			}
			else if( h_Block::Form( blocks[z]->Type()) == h_BlockForm::Plate )
			{
				auto nonstandatd_form_block= static_cast<const h_NonstandardFormBlock*>(blocks[z]);
				float z0= float(z);
				float z1= z0 + 0.5f;
				if( nonstandatd_form_block->Direction() == h_Direction::Down )
				{
					z0+= 0.5f;
					z1+= 0.5f;
				}

				phys_mesh.upper_block_faces.emplace_back(
					x + X, y + Y, z0, h_Direction::Down );

				phys_mesh.upper_block_faces.emplace_back(
					x + X, y + Y, z1, h_Direction::Up );

				for( unsigned int d= 0; d < 6; d++ )
				{
					phys_mesh.block_sides.emplace_back(
						x + X, y + Y,
						z0, z1,
						static_cast<h_Direction>(d + (unsigned int)h_Direction::Forward) );
				}
			}
			else if( h_Block::Form( blocks[z]->Type()) == h_BlockForm::Bisected )
			{
				auto nonstandatd_form_block= static_cast<const h_NonstandardFormBlock*>(blocks[z]);

				p_UpperBlockFace help_face( x + X, y + Y, float(z), h_Direction::Up );

				static const unsigned int c_rot_table[]=
				{ 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5 };
				static const unsigned int c_dir_to_rot_table[6]=
				{ 0, 3,  1, 4,  5, 2 };
				unsigned int rot= c_dir_to_rot_table[
					(unsigned int) nonstandatd_form_block->Direction() -
					(unsigned int) h_Direction::Forward ];

				m_Vec2 vertices[4];
				vertices[0]= help_face.vertices[ c_rot_table[0 + rot] ];
				vertices[1]= help_face.vertices[ c_rot_table[1 + rot] ];
				vertices[2]= help_face.vertices[ c_rot_table[2 + rot] ];
				vertices[3]= help_face.vertices[ c_rot_table[3 + rot] ];

				for( unsigned int i= 0; i < 2; i++ )
				{
					phys_mesh.upper_block_faces.emplace_back();
					p_UpperBlockFace& face= phys_mesh.upper_block_faces.back();

					face.vertex_count= 4;
					face.vertices[0]= vertices[0];
					face.vertices[1]= vertices[1];
					face.vertices[2]= vertices[2];
					face.vertices[3]= vertices[3];
					face.center= help_face.center;
					face.radius= help_face.radius;
					face.z= float(z+i);
					face.dir= i == 0 ? h_Direction::Down : h_Direction::Up;
				}

				for( unsigned int i= 0; i < 4; i++ )
				{
					phys_mesh.block_sides.emplace_back();
					p_BlockSide& side= phys_mesh.block_sides.back();

					side.z0= float(z);
					side.z1= float(z+1);

					static const h_Direction c_circle_table[]=
					{
						h_Direction::ForwardLeft, h_Direction::Forward, h_Direction::ForwardRight,
						h_Direction::BackRight, h_Direction::Back, h_Direction::BackLeft,
					};
					unsigned int s= i == 3 ? 4 : i;
					side.dir= h_Direction( c_circle_table[ (rot + s) % 6 ] );

					side.edge[0]= vertices[i];
					side.edge[1]= vertices[ (i+1) & 3 ];
				}
			} // h_BlockForm::Bisected
		} // for z
	} // for xy

	std::lock_guard<std::mutex> lock( phys_mesh_mutex_ );
	phys_mesh_= std::make_shared< p_WorldPhysMesh >( std::move(phys_mesh ) );
}

void h_World::BlastBlock_r( int x, int y, int z, int blast_power )
{
	if( blast_power == 0 )
		return;

	h_Chunk* ch= GetChunk( x>>H_CHUNK_WIDTH_LOG2, y>>H_CHUNK_WIDTH_LOG2 );
	unsigned int addr;

	addr= BlockAddr( x&(H_CHUNK_WIDTH-1), y&(H_CHUNK_WIDTH-1), z );
	if( ch->blocks_[addr]->Type() != h_BlockType::Water )
		ch->SetBlock( addr, NormalBlock(h_BlockType::Air) );

	//BlastBlock_r( x, y, z + 1, blast_power-1 );
	//BlastBlock_r( x, y, z - 1, blast_power-1 );
	BlastBlock_r( x, y+1, z, blast_power-1 );
	BlastBlock_r( x, y-1, z, blast_power-1 );
	BlastBlock_r( x+1, y+((x+1)&1), z, blast_power-1 );
	BlastBlock_r( x+1, y-(x&1), z, blast_power-1 );
	BlastBlock_r( x-1, y+((x+1)&1), z, blast_power-1 );
	BlastBlock_r( x-1, y-(x&1), z, blast_power-1 );
}

bool h_World::InBorders( int x, int y, int z ) const
{
	bool outside=
		x < 0 || y < 0 ||
		x > int(H_CHUNK_WIDTH * ChunkNumberX()) || y > int(H_CHUNK_WIDTH * ChunkNumberY()) ||
		z < 0 || z >= H_CHUNK_HEIGHT;
	return !outside;
}

bool h_World::CanBuild( int x, int y, int z ) const
{
	return
		GetChunk( x>> H_CHUNK_WIDTH_LOG2, y>> H_CHUNK_WIDTH_LOG2 )->
		GetBlock( x & (H_CHUNK_WIDTH - 1), y & (H_CHUNK_WIDTH - 1), z )->Type() == h_BlockType::Air;
}

void h_World::PhysTick()
{
	while(!phys_thread_need_stop_.load())
	{
		while(phys_thread_paused_.load())
			hSleep( g_sleep_interval_on_pause );

		H_ASSERT( player_ );
		H_ASSERT( renderer_ );

		TestMobTick();

		uint64_t t0_ms = hGetTimeMS();

		// Build/destroy.
		FlushActionQueue();

		// Blocks failing. Do it before water phys tick.
		// If block was removed, it must be replaced by upper failing blocks, and only AFTER it water can flow to this palce.
		{
			for( unsigned int y= active_area_margins_[1]; y < chunk_number_y_ - active_area_margins_[1]; y++ )
			for( unsigned int x= active_area_margins_[0]; x < chunk_number_x_ - active_area_margins_[0]; x++ )
				GetChunk( x, y )->ProcessFailingBlocks();
		}

		WaterPhysTick();
		GrassPhysTick();
		FirePhysTick();
		RelightWaterModifedChunksLight();
		RainTick();

		// player logic
		{
			m_Vec3 player_pos= player_->EyesPos();
			int player_coord_global[2];
			pGetHexogonCoord( player_pos.xy(), &player_coord_global[0], &player_coord_global[1] );

			int player_coord[3];
			player_coord[0]= player_coord_global[0] - Longitude() * H_CHUNK_WIDTH;
			player_coord[1]= player_coord_global[1] - Latitude () * H_CHUNK_WIDTH;
			player_coord[2]= int( std::round(player_pos.z) );
			UpdatePhysMesh(
				player_coord[0] - 5, player_coord[0] + 5,
				player_coord[1] - 6, player_coord[1] + 6,
				player_coord[2] - 5, player_coord[2] + 5 );

			int player_chunk_x= ( player_coord[0] + (H_CHUNK_WIDTH>>1) ) >> H_CHUNK_WIDTH_LOG2;
			int player_chunk_y= ( player_coord[1] + (H_CHUNK_WIDTH>>1) ) >> H_CHUNK_WIDTH_LOG2;

			if( player_chunk_y > int(chunk_number_y_/2+2) )
				MoveWorld( NORTH );
			else if( player_chunk_y < int(chunk_number_y_/2-2) )
				MoveWorld( SOUTH );
			if( player_chunk_x > int(chunk_number_x_/2+2) )
				MoveWorld( EAST );
			else if( player_chunk_x < int(chunk_number_x_/2-2) )
				MoveWorld( WEST );
		}

		{ // Modify phys ticks counter under mutex only.
			std::lock_guard<std::mutex> lock( phys_tick_count_mutex_ );
			phys_tick_count_++;
		}

		renderer_->Update();

		uint64_t t1_ms= hGetTimeMS();
		unsigned int dt_ms= (unsigned int)(t1_ms - t0_ms);
		if (dt_ms < g_update_inrerval_ms)
			hSleep( g_update_inrerval_ms - dt_ms );
	}
}

void h_World::TestMobTick()
{
	if( phys_tick_count_ - test_mob_last_think_tick_ >= g_updates_frequency / 3u )
	{
		test_mob_last_think_tick_= phys_tick_count_;

		if( test_mob_discret_pos_[0] != test_mob_target_pos_[0] ||
			test_mob_discret_pos_[1] != test_mob_target_pos_[1] ||
			test_mob_discret_pos_[2] != test_mob_target_pos_[2] )
		{
			h_PathFinder finder(*this);
			if( finder.FindPath(
				test_mob_discret_pos_[0], test_mob_discret_pos_[1], test_mob_discret_pos_[2],
				test_mob_target_pos_[0], test_mob_target_pos_[1], test_mob_target_pos_[2] ) )
			{
				const h_PathPoint* path= finder.GetPathPoints() + finder.GetPathLength() - 1;
				test_mob_discret_pos_[0]= path->x;
				test_mob_discret_pos_[1]= path->y;
				test_mob_discret_pos_[2]= path->z;
			}
		}
	}

	test_mob_pos_.x= ( float(test_mob_discret_pos_[0]) + 1.0f / 3.0f ) * H_SPACE_SCALE_VECTOR_X;
	test_mob_pos_.y= float(test_mob_discret_pos_[1]) + 0.5f * float((test_mob_discret_pos_[0]^1)&1);
	test_mob_pos_.z= float(test_mob_discret_pos_[2]);
}

void h_World::WaterPhysTick()
{
	const m_Vec3 player_pos= player_->EyesPos();
	int player_coord_global[2];
	pGetHexogonCoord( player_pos.xy(), &player_coord_global[0], &player_coord_global[1] );

	int player_chunk[2];
	player_chunk[0]= ( player_coord_global[0] - Longitude() * H_CHUNK_WIDTH ) >> H_CHUNK_WIDTH_LOG2;
	player_chunk[1]= ( player_coord_global[1] - Latitude () * H_CHUNK_WIDTH ) >> H_CHUNK_WIDTH_LOG2;

	for( unsigned int i= active_area_margins_[0]; i< ChunkNumberX() - active_area_margins_[0]; i++ )
	for( unsigned int j= active_area_margins_[1]; j< ChunkNumberY() - active_area_margins_[1]; j++ )
	{
		// More rarely update distant water chunks.
		const int distance_to_player= std::abs( int(i) - player_chunk[0] ) + std::abs( int(j) - player_chunk[1] );
		if( distance_to_player > 4 )
		{
			if( ( phys_tick_count_ & 2 ) != 0 ) continue; // Update each 2 ticks
		}
		if( distance_to_player > 8 )
		{
			if( ( phys_tick_count_ & 4 ) != 0 ) continue; // Update each 4 ticks
		}

		// Update only each second cluster of size 3x3 per tick.
		int cluster_x= m_Math::DivNonNegativeRemainder(int(i) + longitude_, 3);
		int cluster_y= m_Math::DivNonNegativeRemainder(int(j) + latitude_ , 3);
		if( ( (cluster_x ^ cluster_y) & 1 ) == ( phys_tick_count_ & 1 ) )
			continue;

		bool chunk_modifed= false;
		h_Chunk* ch= GetChunk( i, j );

		int X= i << H_CHUNK_WIDTH_LOG2;
		int Y= j << H_CHUNK_WIDTH_LOG2;

		std::vector< h_LiquidBlock* >& list= ch->water_block_list_;
		for( unsigned int k= 0; k < list.size(); )
		{
			h_LiquidBlock* b= list[k];
			k++;

			H_ASSERT( ch->GetBlock( b->x_, b->y_, b->z_ ) == b );

			unsigned int block_addr= BlockAddr( b->x_, b->y_, b->z_ );
			h_Block* lower_block= ch->GetBlock( block_addr - 1 );

			// Try fail down.
			if( lower_block->Type() == h_BlockType::Air )
			{
				ch->SetBlock( block_addr, NormalBlock( h_BlockType::Air ) );
				ch->SetBlock( block_addr - 1, b );
				b->z_--;

				chunk_modifed= true;

				// If we fail, flow in next tick.
				continue;
			}
			else
			{
				// Try flow down.
				if( lower_block->Type() == h_BlockType::Water )
				{
					h_LiquidBlock* lower_water_block= static_cast<h_LiquidBlock*>(lower_block);

					int level_delta= std::min(int(H_MAX_WATER_LEVEL - lower_water_block->LiquidLevel()), int(b->LiquidLevel()));
					if( level_delta > 0 )
					{
						b->DecreaseLiquidLevel( level_delta );
						lower_water_block->IncreaseLiquidLevel( level_delta );
						chunk_modifed= true;
					}
				}

				int global_x= X + b->x_;
				int global_y= Y + b->y_;

				int forward_side_y= global_y + ( (global_x^1) & 1 );
				int back_side_y= global_y - (global_x & 1);

				int neighbors[6][2]=
				{
					{ global_x, global_y + 1 },
					{ global_x, global_y - 1 },
					{ global_x + 1, forward_side_y },
					{ global_x + 1, back_side_y },
					{ global_x - 1, forward_side_y },
					{ global_x - 1, back_side_y },
				};

				for( unsigned int d= 0; d < 6; d++ )
					if( WaterFlow( b, neighbors[d][0], neighbors[d][1], b->z_ ) )
						chunk_modifed= true;

				if( b->LiquidLevel() == 0 ||
					( b->LiquidLevel() < 16 && lower_block->Type() != h_BlockType::Water ) )
				{
					ch->SetBlock( block_addr, NormalBlock( h_BlockType::Air ) );
					CheckBlockNeighbors( global_x, global_y, b->z_ );

					ch->DeleteWaterBlock( b );

					k--;
					list[k]= list.back();
					list.pop_back();

					chunk_modifed= true;
				}
			}// if down block not air
		}//for all water blocks in chunk

		if( chunk_modifed )
		{
			renderer_->UpdateChunkWater( i  , j   );

			renderer_->UpdateChunkWater( i-1, j   );
			renderer_->UpdateChunkWater( i+1, j   );
			renderer_->UpdateChunkWater( i  , j-1 );
			renderer_->UpdateChunkWater( i  , j+1 );

			renderer_->UpdateChunkWater( i-1, j-1 );
			renderer_->UpdateChunkWater( i-1, j+1 );
			renderer_->UpdateChunkWater( i+1, j-1 );
			renderer_->UpdateChunkWater( i+1, j+1 );

			ch->need_update_light_= true;
		}
	}//for chunks
}

bool h_World::WaterFlow( h_LiquidBlock* from, int to_x, int to_y, int to_z )
{
	int local_x= to_x & ( H_CHUNK_WIDTH-1 );
	int local_y= to_y & ( H_CHUNK_WIDTH-1 );

	h_Chunk* ch= GetChunk( to_x >> H_CHUNK_WIDTH_LOG2, to_y >> H_CHUNK_WIDTH_LOG2 );
	int addr= BlockAddr( local_x, local_y, to_z );
	h_Block* block= ch->GetBlock( addr );

	if( block->Type() == h_BlockType::Air || block->Type() == h_BlockType::Fire )
	{
		if( from->LiquidLevel() > 1 )
		{
			if( block->Type() == h_BlockType::Fire )
				RemoveFire( to_x, to_y, to_z );

			int level_delta= from->LiquidLevel() / 2;
			from->DecreaseLiquidLevel( level_delta );

			h_LiquidBlock* new_block= ch->NewWaterBlock();
			new_block->x_= local_x;
			new_block->y_= local_y;
			new_block->z_= to_z;
			new_block->SetLiquidLevel( level_delta );
			ch->SetBlock( addr, new_block );

			CheckBlockNeighbors( to_x, to_y, to_z );
			return true;
		}
	}
	else if( block->Type() == h_BlockType::Water )
	{
		h_LiquidBlock* water_block= static_cast<h_LiquidBlock*>(block);

		int water_level_delta= from->LiquidLevel() - water_block->LiquidLevel();
		if( water_level_delta > 1 )
		{
			water_level_delta/= 2;
			from->DecreaseLiquidLevel( water_level_delta );
			water_block->IncreaseLiquidLevel( water_level_delta );
			return true;
		}
	}
	return false;
}

void h_World::GrassPhysTick()
{
	const unsigned int c_reproducing_start_chance= m_Rand::max_rand / 32;
	const unsigned int c_reproducing_do_chance= m_Rand::max_rand / 12;
	const unsigned char c_min_light_for_grass_reproducing= H_MAX_SUN_LIGHT / 2;

	m_Vec3 sun_vector= calendar_.GetSunVector( phys_tick_count_, GetGlobalWorldLatitude() );
	unsigned char current_sun_multiplier= sun_vector.z > std::sin( 4.0f * m_Math::deg2rad ) ? 1 : 0;

	for( unsigned int y= active_area_margins_[1]; y < chunk_number_y_ - active_area_margins_[1]; y++ )
	for( unsigned int x= active_area_margins_[0]; x < chunk_number_x_ - active_area_margins_[0]; x++ )
	{
		h_Chunk* chunk= GetChunk( x, y );
		int X= x << H_CHUNK_WIDTH_LOG2;
		int Y= y << H_CHUNK_WIDTH_LOG2;

		auto& blocks= chunk->active_grass_blocks_;
		for( unsigned int i= 0; i < blocks.size(); )
		{
			h_GrassBlock* grass_block= blocks[i];

			H_ASSERT( grass_block->IsActive() );
			H_ASSERT( grass_block->GetZ() > 0 );

			int block_addr= BlockAddr( grass_block->GetX(), grass_block->GetY(), grass_block->GetZ() );
			H_ASSERT( block_addr <= H_CHUNK_WIDTH * H_CHUNK_WIDTH * H_CHUNK_HEIGHT );

			H_ASSERT( chunk->blocks_[ block_addr ] == grass_block );

			// Grass fade, if upper block is full or it is water.
			h_Block* upper_block= chunk->blocks_[ block_addr + 1 ];
			if( ( upper_block->CombinedTransparency() & H_VISIBLY_TRANSPARENCY_BITS ) == TRANSPARENCY_SOLID ||
				upper_block->Type() == h_BlockType::Water )
			{
				chunk->blocks_[ block_addr ]= NormalBlock( h_BlockType::Soil );

				chunk->active_grass_blocks_allocator_.Delete( grass_block );

				if( i != blocks.size() - 1 ) blocks[i]= blocks.back();
				blocks.pop_back();

				renderer_->UpdateChunk( x, y );

				continue;
			}

			unsigned char light=
				chunk-> sun_light_map_[ block_addr + 1 ] * current_sun_multiplier +
				chunk->fire_light_map_[ block_addr + 1 ];

			if( light >= c_min_light_for_grass_reproducing &&
				phys_processes_rand_.Rand() <= c_reproducing_start_chance )
			{
				bool can_reproduce= false;

				bool z_plus_2_block_is_air= chunk->blocks_[ block_addr + 2 ]->Type() == h_BlockType::Air;

				int world_x= grass_block->GetX() + X;
				int world_y= grass_block->GetY() + Y;

				int forward_side_y= world_y + ( (world_x^1) & 1 );
				int back_side_y= world_y - (world_x & 1);

				int neighbors[6][2]=
				{
					{ world_x, world_y + 1 },
					{ world_x, world_y - 1 },
					{ world_x + 1, forward_side_y },
					{ world_x + 1, back_side_y },
					{ world_x - 1, forward_side_y },
					{ world_x - 1, back_side_y },
				};

				for( unsigned int n= 0; n < 6; n++ )
				{
					int neinghbor_chunk_x= neighbors[n][0] >> H_CHUNK_WIDTH_LOG2;
					int neinghbor_chunk_y= neighbors[n][1] >> H_CHUNK_WIDTH_LOG2;

					h_Chunk* neighbor_chunk= GetChunk( neinghbor_chunk_x, neinghbor_chunk_y );

					int local_x= neighbors[n][0] & (H_CHUNK_WIDTH-1);
					int local_y= neighbors[n][1] & (H_CHUNK_WIDTH-1);
					int neighbor_addr= BlockAddr( local_x, local_y, grass_block->GetZ() );
					H_ASSERT( neighbor_addr <= H_CHUNK_WIDTH * H_CHUNK_WIDTH * H_CHUNK_HEIGHT );

					h_BlockType z_minus_one_block_type= neighbor_chunk->blocks_[ neighbor_addr - 1 ]->Type();
					h_BlockType neighbor_block_type   = neighbor_chunk->blocks_[ neighbor_addr     ]->Type();
					h_BlockType z_plus_one_block_type = neighbor_chunk->blocks_[ neighbor_addr + 1 ]->Type();
					h_BlockType z_plus_two_block_type = neighbor_chunk->blocks_[ neighbor_addr + 2 ]->Type();

					if( z_minus_one_block_type == h_BlockType::Soil &&
						neighbor_block_type    == h_BlockType::Air &&
						z_plus_one_block_type  == h_BlockType::Air )
					{
						if( phys_processes_rand_.Rand() <= c_reproducing_do_chance )
						{
							neighbor_chunk->blocks_[ neighbor_addr - 1 ]=
								neighbor_chunk->NewActiveGrassBlock(
									local_x, local_y, grass_block->GetZ() - 1 );

							renderer_->UpdateChunk( neinghbor_chunk_x, neinghbor_chunk_y );
						}
						can_reproduce= true;
					}
					if( neighbor_block_type    == h_BlockType::Soil &&
						z_plus_one_block_type  == h_BlockType::Air )
					{
						if( phys_processes_rand_.Rand() <= c_reproducing_do_chance )
						{
							neighbor_chunk->blocks_[ neighbor_addr  ]=
								neighbor_chunk->NewActiveGrassBlock(
									local_x, local_y, grass_block->GetZ() );

							renderer_->UpdateChunk( neinghbor_chunk_x, neinghbor_chunk_y );
						}
						can_reproduce= true;
					}
					if( z_plus_one_block_type  == h_BlockType::Soil &&
						z_plus_two_block_type  == h_BlockType::Air &&
						z_plus_2_block_is_air )
					{
						if( phys_processes_rand_.Rand() <= c_reproducing_do_chance )
						{
							neighbor_chunk->blocks_[ neighbor_addr + 1 ]=
								neighbor_chunk->NewActiveGrassBlock(
									local_x, local_y, grass_block->GetZ() + 1 );

							renderer_->UpdateChunk( neinghbor_chunk_x, neinghbor_chunk_y );
						}
						can_reproduce= true;
					}

				} // for neighbors

				if( !can_reproduce )
				{
					// Deactivate grass block
					chunk->blocks_[ block_addr ]= &unactive_grass_block_;

					chunk->active_grass_blocks_allocator_.Delete( grass_block );

					if( i != blocks.size() - 1 ) blocks[i]= blocks.back();
					blocks.pop_back();

					continue;
				}

			} // if rand

			i++;
		} // for grass blocks
	} // for chunks
}

void h_World::FirePhysTick()
{
	const unsigned int c_min_fire_activation_power= h_Fire::c_max_power_ / 6;
	const unsigned int c_fire_activation_chanse = m_Rand::max_rand / 10;
	const unsigned int c_near_block_burn_base_chance= m_Rand::max_rand / 8;
	const unsigned int c_up_down_blocks_burn_base_chanse[]=
	{
		m_Rand::max_rand / 12, m_Rand::max_rand / 6,
	};

	unsigned int c_rain_check_base_chance= m_Rand::max_rand / 24;

	auto gen_neighbors=
	[]( int x, int y, int neighbors[6][2] )
	{
		int forward_side_y= y + ( (x^1) & 1 );
		int back_side_y= y - (x & 1);

		neighbors[0][0]= x    ; neighbors[0][1]= y + 1;
		neighbors[1][0]= x    ; neighbors[1][1]= y - 1;
		neighbors[2][0]= x + 1; neighbors[2][1]= forward_side_y;
		neighbors[3][0]= x + 1; neighbors[3][1]= back_side_y;
		neighbors[4][0]= x - 1; neighbors[4][1]= forward_side_y;
		neighbors[5][0]= x - 1; neighbors[5][1]= back_side_y;
	};

	auto try_place_fire=
	[this, &gen_neighbors]( int x, int y, int z, unsigned int base_chance )
	{
		h_Chunk* ch= GetChunk(
			x >> H_CHUNK_WIDTH_LOG2,
			y >> H_CHUNK_WIDTH_LOG2 );
		int local_x= x & (H_CHUNK_WIDTH - 1);
		int local_y= y & (H_CHUNK_WIDTH - 1);

		int addr= BlockAddr( local_x, local_y, z );
		H_ASSERT( ch->GetBlock(addr)->Type() == h_BlockType::Air );

		unsigned int max_flammability= 0;
		max_flammability= std::max<unsigned int>( max_flammability, ch->GetBlock( addr + 1 )->Flammability() );
		max_flammability= std::max<unsigned int>( max_flammability, ch->GetBlock( addr - 1 )->Flammability() );

		int neighbors[6][2];
		gen_neighbors( x, y, neighbors );
		for( unsigned int n= 0; n < 6; n++ )
		{
			h_Chunk* ch2= GetChunk(
				neighbors[n][0] >> H_CHUNK_WIDTH_LOG2,
				neighbors[n][1] >> H_CHUNK_WIDTH_LOG2 );

			h_Block* b= ch2->GetBlock(
				neighbors[n][0] & (H_CHUNK_WIDTH - 1),
				neighbors[n][1] & (H_CHUNK_WIDTH - 1),
				z );

			max_flammability= std::max<unsigned int>( max_flammability, b->Flammability() );
		}

		if(
			H_MAX_FLAMMABILITY * phys_processes_rand_.Rand() >=
			max_flammability * base_chance )
			return;

		h_Fire* new_fire= new h_Fire();
		new_fire->x_= local_x;
		new_fire->y_= local_y;
		new_fire->z_= z;

		ch->light_source_list_.push_back( new_fire );
		ch->fire_list_.push_back( new_fire );
		ch->SetBlock( addr, new_fire );

		unsigned int light_level= new_fire->LightLevel();
		AddFireLight_r( x, y, z, light_level );
		UpdateInRadius( x, y, light_level );
		UpdateWaterInRadius( x, y, light_level );
	};

	auto can_pace_fire=
	[this, &gen_neighbors]( int x, int y, int z ) -> bool
	{
		h_Chunk* ch= GetChunk(
			x >> H_CHUNK_WIDTH_LOG2,
			y >> H_CHUNK_WIDTH_LOG2 );
		int local_x= x & (H_CHUNK_WIDTH - 1);
		int local_y= y & (H_CHUNK_WIDTH - 1);

		int addr= BlockAddr( local_x, local_y, z );

		unsigned int max_flammability= 0;
		max_flammability= std::max<unsigned int>( max_flammability, ch->GetBlock( addr + 1 )->Flammability() );
		max_flammability= std::max<unsigned int>( max_flammability, ch->GetBlock( addr - 1 )->Flammability() );

		int neighbors[6][2];
		gen_neighbors( x, y, neighbors );
		for( unsigned int n= 0; n < 6; n++ )
		{
			h_Chunk* ch2= GetChunk(
				neighbors[n][0] >> H_CHUNK_WIDTH_LOG2,
				neighbors[n][1] >> H_CHUNK_WIDTH_LOG2 );

			h_Block* b= ch2->GetBlock(
				neighbors[n][0] & (H_CHUNK_WIDTH - 1),
				neighbors[n][1] & (H_CHUNK_WIDTH - 1),
				z );

			max_flammability= std::max<unsigned int>( max_flammability, b->Flammability() );
		}

		return max_flammability > 0;
	};

	auto place_fire=
	[this]( int x, int y, int z )
	{
		h_Chunk* ch= GetChunk(
			x >> H_CHUNK_WIDTH_LOG2,
			y >> H_CHUNK_WIDTH_LOG2 );
		int local_x= x & (H_CHUNK_WIDTH - 1);
		int local_y= y & (H_CHUNK_WIDTH - 1);

		int addr= BlockAddr( local_x, local_y, z );
		H_ASSERT( ch->GetBlock(addr)->Type() == h_BlockType::Air );

		h_Fire* new_fire= new h_Fire();
		new_fire->x_= local_x;
		new_fire->y_= local_y;
		new_fire->z_= z;

		ch->light_source_list_.push_back( new_fire );
		ch->fire_list_.push_back( new_fire );
		ch->SetBlock( addr, new_fire );

		unsigned int light_level= new_fire->LightLevel();
		AddFireLight_r( x, y, z, light_level );
		UpdateInRadius( x, y, light_level );
		UpdateWaterInRadius( x, y, light_level );
	};

	// Try add fire blocks.
	for( unsigned int y= active_area_margins_[1]; y < chunk_number_y_ - active_area_margins_[1]; y++ )
	for( unsigned int x= active_area_margins_[0]; x < chunk_number_x_ - active_area_margins_[0]; x++ )
	{
		h_Chunk* chunk= GetChunk( x, y );
		int X= x << H_CHUNK_WIDTH_LOG2;
		int Y= y << H_CHUNK_WIDTH_LOG2;

		std::vector< h_Fire* >& fire_list= chunk->fire_list_;
		for( unsigned int i= 0; i < fire_list.size(); i++ )
		{
			h_Fire* fire= fire_list[i];

			if( fire->power_ < h_Fire::c_max_power_ )
				fire->power_++;

			if( fire->power_ < c_min_fire_activation_power ||
				phys_processes_rand_.Rand() >= c_fire_activation_chanse * fire->power_ / h_Fire::c_max_power_ )
				continue;

			int fire_global_x= X + fire->x_;
			int fire_global_y= Y + fire->y_;

			int fire_addr= BlockAddr( fire->x_, fire->y_, fire->z_ );
			bool up_down_is_air[2]=
			{
				chunk->GetBlock( fire_addr - 1 )->Type() == h_BlockType::Air,
				chunk->GetBlock( fire_addr + 1 )->Type() == h_BlockType::Air,
			};

			unsigned int current_up_down_burn_base_chance[2]=
			{
				c_up_down_blocks_burn_base_chanse[0] * fire->power_ / h_Fire::c_max_power_,
				c_up_down_blocks_burn_base_chanse[1] * fire->power_ / h_Fire::c_max_power_,
			};
			unsigned int current_near_block_burn_base_chance=
				c_near_block_burn_base_chance * fire->power_ / h_Fire::c_max_power_;

			int neighbors[6][2];
			gen_neighbors( fire_global_x, fire_global_y, neighbors );
			for( unsigned int n= 0; n < 6; n++ )
			{
				h_Chunk* ch2= GetChunk(
					neighbors[n][0] >> H_CHUNK_WIDTH_LOG2,
					neighbors[n][1] >> H_CHUNK_WIDTH_LOG2 );

				int local_x= neighbors[n][0] & (H_CHUNK_WIDTH - 1);
				int local_y= neighbors[n][1] & (H_CHUNK_WIDTH - 1);
				int addr= BlockAddr( local_x, local_y, fire->z_ );

				bool near_block_is_air= ch2->GetBlock( addr )->Type() == h_BlockType::Air;

				// Try burn near block.
				if(
					H_MAX_FLAMMABILITY * phys_processes_rand_.Rand() <
					ch2->GetBlock( addr )->Flammability() * current_near_block_burn_base_chance )
				{
					ch2->SetBlock( addr, NormalBlock( h_BlockType::Air ) );
					RelightBlockRemove( neighbors[n][0], neighbors[n][1], fire->z_ );
					place_fire( neighbors[n][0], neighbors[n][1], fire->z_ );

					CheckBlockNeighbors( neighbors[n][0], neighbors[n][1], fire->z_ );
				}
				// Try move fire to near block.
				else if( near_block_is_air )
					try_place_fire(
						neighbors[n][0], neighbors[n][1], fire->z_,
						current_near_block_burn_base_chance );

				// Try move fire to upper/lower near blocks.
				for( int dz= -1; dz <= 1; dz+= 2 )
				{
					unsigned int z_index= (dz + 1) >> 1;
					int z= fire->z_ + dz;

					bool is_path= up_down_is_air[ z_index ] || near_block_is_air;

					if( is_path &&
						ch2->GetBlock( addr + dz )->Type() == h_BlockType::Air )
						try_place_fire(
							neighbors[n][0], neighbors[n][1], z,
							current_up_down_burn_base_chance[ z_index ] );
				} // for z
			} // for fire neighbors

			// Process up and down blocks.
			for( int dz = -1; dz <= 1; dz+= 2 )
			{
				unsigned int z_index= (dz + 1) >> 1;
				int z= fire->z_ + dz;

				// Try burn near block.
				if( H_MAX_FLAMMABILITY * phys_processes_rand_.Rand() <
					chunk->GetBlock( fire_addr + dz )->Flammability() * current_near_block_burn_base_chance )
				{
					chunk->SetBlock( fire_addr + dz, NormalBlock( h_BlockType::Air ) );
					RelightBlockRemove( fire_global_x, fire_global_y, z );
					place_fire( fire_global_x, fire_global_y, z );

					CheckBlockNeighbors( fire_global_x, fire_global_y, z );
				}
				// Try move fire to near block.
				else if( up_down_is_air[ z_index ] )
					try_place_fire(
						fire_global_x, fire_global_y, z,
						current_up_down_burn_base_chance[ z_index ] );
			}

		} // for fire blocks
	} // for xy chunks

	float current_rain_intensity= rain_data_.current_intensity.load();
	bool is_rain= current_rain_intensity > 0.0f;
	unsigned int rain_check_chance= (unsigned int)( float( c_rain_check_base_chance ) * current_rain_intensity );

	// Remove fire blocks
	for( unsigned int y= active_area_margins_[1]; y < chunk_number_y_ - active_area_margins_[1]; y++ )
	for( unsigned int x= active_area_margins_[0]; x < chunk_number_x_ - active_area_margins_[0]; x++ )
	{
		h_Chunk* chunk= GetChunk( x, y );
		int X= x << H_CHUNK_WIDTH_LOG2;
		int Y= y << H_CHUNK_WIDTH_LOG2;

		std::vector< h_Fire* >& fire_list= chunk->fire_list_;
		for( unsigned int i= 0; i < fire_list.size(); )
		{
			h_Fire* fire= fire_list[i];
			i++;

			bool is_extinguished= false;

			if( is_rain &&
				phys_processes_rand_.Rand() < rain_check_chance )
			{
				bool is_sky= true;

				h_Block** blocks= chunk->blocks_ + BlockAddr( fire->x_, fire->y_, 0 );
				for( int z= fire->z_ + 1; z < H_CHUNK_HEIGHT - 1; z++ )
					if( blocks[z]->Type() != h_BlockType::Air )
					{
						is_sky= false;
						break;
					}

				is_extinguished= is_sky;
			}

			int global_x= X + fire->x_;
			int global_y= Y + fire->y_;
			if( is_extinguished ||
				chunk->GetBlock( fire->x_, fire->y_, fire->z_ + 1 )->Type() == h_BlockType::Water ||
				!can_pace_fire( global_x, global_y, fire->z_ ) )
			{
				int local_x= fire->x_;
				int local_y= fire->y_;
				int z= fire->z_;

				chunk->DeleteLightSource( fire );
				chunk->SetBlock( local_x, local_y, z, NormalBlock( h_BlockType::Air ) );

				i--;
				if( i + 1 != fire_list.size() )
					fire_list[i]= fire_list.back();
				fire_list.pop_back();

				int r= chunk->FireLightLevel( local_x, local_y, z );
				RelightBlockAdd( global_x, global_y, z );
				RelightBlockRemove( global_x, global_y, z );

				UpdateInRadius( global_x, global_y, r );
				UpdateWaterInRadius( global_x, global_y, r );
			}
		} // for fire blocks
	} // for xy chunks
}

void h_World::RainTick()
{
	static constexpr unsigned int c_rain_try_start_interval_ticks= 6 * g_updates_frequency;

	// Chanse of rain start for N attempts is "1 - (1 - start_chanse) ^ N"
	static constexpr unsigned int c_rain_start_chanse= rain_data_.rand_generator.max() /256;

	static constexpr unsigned int c_middle_rain_duration_ticks= g_day_duration_ticks / 8;
	static constexpr unsigned int c_min_rain_duration_ticks= g_day_duration_ticks / 16;
	static constexpr unsigned int c_max_rain_duration_ticks= g_day_duration_ticks * 3 / 2;

	static constexpr unsigned int c_rain_edge_time_ticks= 10 * g_updates_frequency;
	static_assert( c_rain_edge_time_ticks * 2 < c_min_rain_duration_ticks, "Invalid rain parameters" );

 	if( !rain_data_.is_rain )
	{
		if( phys_tick_count_ % c_rain_try_start_interval_ticks == 0 )
		{
			if( rain_data_.rand_generator() < c_rain_start_chanse )
			{
				rain_data_.is_rain= true;
				rain_data_.start_tick= phys_tick_count_;

				// c_middle_rain_duration_ticks= k * e ^ ( pow * pow * 0.5 )
				const float k=
					float(c_middle_rain_duration_ticks) /
					std::exp( rain_data_.c_duration_rand_pow * rain_data_.c_duration_rand_pow * 0.5f );

				rain_data_.duration= (unsigned int)( k * rain_data_.duration_rand( rain_data_.rand_generator ) );
				rain_data_.duration=
					std::min(
						c_max_rain_duration_ticks,
						std::max(
							c_min_rain_duration_ticks,
							rain_data_.duration ) );

				rain_data_.base_intensity= rain_data_.intensity_rand( rain_data_.rand_generator );
			}
		}
	}

	if( rain_data_.is_rain )
	{
		unsigned int ticks_since_rain_start= phys_tick_count_ - rain_data_.start_tick;

		if( ticks_since_rain_start >= rain_data_.duration )
		{
			rain_data_.is_rain= false;
			rain_data_.current_intensity.store( 0.0f );
		}
		else
		{
			float current_intencity;

			if( ticks_since_rain_start < c_rain_edge_time_ticks )
				current_intencity= float( ticks_since_rain_start ) / float( c_rain_edge_time_ticks );
			else if( rain_data_.duration - ticks_since_rain_start < c_rain_edge_time_ticks )
				current_intencity= float( rain_data_.duration - ticks_since_rain_start ) / float( c_rain_edge_time_ticks );
			else
				current_intencity= 1.0f;

			rain_data_.current_intensity.store( rain_data_.base_intensity * current_intencity );
		}

	}
}

void h_World::InitNormalBlocks()
{
	for( size_t i= 0; i < size_t(h_BlockType::NumBlockTypes); i++ )
		new ( normal_blocks_ + i ) h_Block( h_BlockType( static_cast<h_BlockType>(i) ) );
}
