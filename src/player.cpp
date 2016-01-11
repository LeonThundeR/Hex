#include "player.hpp"
#include "block_collision.hpp"
#include "world_header.hpp"
#include "world.hpp"
#include "ticks_counter.hpp"

static const float g_acceleration= 40.0f;
static const float g_deceleration= 40.0f;
static const float g_air_acceleration= 2.0f;
static const float g_air_deceleration= 4.0f;
static const float g_vertical_acceleration= -9.8f * 1.5f;
static const float g_max_speed= 5.0f;
static const float g_max_vertical_speed= 30.0f;

static const float g_jump_height= 1.4f;
static const float g_jump_speed= std::sqrt( 2.0f * g_jump_height * -g_vertical_acceleration );

static const float g_max_build_distance= 4.0f;

static const m_Vec3 g_block_normals[8]=
{
	m_Vec3( 0.0f, 1.0f, 0.0f ),      m_Vec3( 0.0f, -1.0f, 0.0f ),
	m_Vec3( +H_SPACE_SCALE_VECTOR_X, 0.5f, 0.0 ), m_Vec3( -H_SPACE_SCALE_VECTOR_X, -0.5f, 0.0 ),
	m_Vec3( -H_SPACE_SCALE_VECTOR_X, 0.5f, 0.0 ), m_Vec3( +H_SPACE_SCALE_VECTOR_X, -0.5f, 0.0 ),
	m_Vec3( 0.0f, 0.0f, 1.0f ),      m_Vec3( 0.0f, 0.0f, -1.0f )
};


h_Player::h_Player(
	const h_WorldPtr& world,
	const h_WorldHeaderPtr& world_header )
	: world_(world)
	, world_header_(world_header)
	, pos_( world_header->player.x, world_header->player.y, world_header->player.z )
	, speed_( 0.0f, 0.0f, 0.0f )
	, vertical_speed_(0.0f)
	, is_flying_(false)
	, in_air_(true)
	, view_angle_( world_header->player.rotation_x, 0.0f, world_header->player.rotation_z )
	, prev_move_time_ms_(0)
	, build_direction_( h_Direction::Unknown )
	, build_block_( h_BlockType::Unknown )
	, player_data_mutex_()
	, phys_mesh_()
{
	if( pos_.z <= 0.0f )
	{
		// TODO - fetch data from world, place player on ground level
		pos_.z= 125.0f;
	}

	build_pos_= pos_;
	discret_build_pos_[0]= discret_build_pos_[1]= discret_build_pos_[2]= 0;
}

h_Player::~h_Player()
{
	world_header_->player.x= pos_.x;
	world_header_->player.y= pos_.y;
	world_header_->player.z= pos_.z;
	world_header_->player.rotation_x= view_angle_.x;
	world_header_->player.rotation_z= view_angle_.z;
}

void h_Player::SetCollisionMesh( h_ChunkPhysMesh mesh )
{
	phys_mesh_= std::move(mesh);
}

void h_Player::Move( const m_Vec3& direction )
{
	uint64_t current_time_ms = hGetTimeMS();
	if( prev_move_time_ms_ == 0 ) prev_move_time_ms_= current_time_ms;
	float dt= float(current_time_ms - prev_move_time_ms_) / 1000.0f;
	prev_move_time_ms_= current_time_ms;

	const float c_eps= 0.001f;

	bool use_ground_acceleration= !in_air_ || is_flying_;

	m_Vec3 move_delta= direction;
	if( !is_flying_ ) move_delta.z= 0.0f;

	float move_delta_length= move_delta.Length();
	if( move_delta_length > c_eps )
	{
		m_Vec3 acceleration_vec= ( use_ground_acceleration ? g_acceleration : g_air_acceleration ) / move_delta_length * move_delta;
		speed_+= acceleration_vec * dt;
	}

	float speed_value= speed_.Length();
	if( speed_value > g_max_speed )
		speed_*= g_max_speed / speed_value;

	if( speed_value > c_eps )
	{
		if( move_delta_length <= c_eps )
		{
			m_Vec3 deceleration_vec= speed_;
			deceleration_vec.Normalize();

			float d_speed= std::min( dt * ( use_ground_acceleration ? g_deceleration : g_air_deceleration ), speed_value );
			speed_-= deceleration_vec * d_speed;
		}
	}
	else
	{
		speed_value= 0.0f;
		speed_= m_Vec3( 0.0f, 0.0f, 0.0f );
	}

	if( !is_flying_ )
	{
		speed_.z= 0.0f;

		vertical_speed_+= g_vertical_acceleration * dt;
		if( vertical_speed_ > g_max_vertical_speed ) vertical_speed_= g_max_vertical_speed;
		else if( -vertical_speed_ > g_max_vertical_speed ) vertical_speed_= -g_max_vertical_speed;
	}
	else vertical_speed_= 0.0f;

	MoveInternal( m_Vec3( speed_.x, speed_.y, speed_.z + vertical_speed_ ) * dt );
}

void h_Player::Rotate( const m_Vec3& delta )
{
	view_angle_+= delta;

	if( view_angle_.z < 0.0f ) view_angle_.z+= m_Math::two_pi;
	else if( view_angle_.z > m_Math::two_pi ) view_angle_.z-= m_Math::two_pi;

	if( view_angle_.x > m_Math::pi_2 ) view_angle_.x= m_Math::pi_2;
	else if( view_angle_.x < -m_Math::pi_2 ) view_angle_.x= -m_Math::pi_2;
}

void h_Player::ToggleFly()
{
	is_flying_= !is_flying_;
}

void h_Player::Jump()
{
	if( !is_flying_ && !in_air_ )
	{
		vertical_speed_+= g_jump_speed;
		in_air_= true;
	}
}

void h_Player::SetBuildBlock( h_BlockType block_type )
{
	build_block_= block_type;
}

void h_Player::Tick()
{
	UpdateBuildPos();

	build_pos_.x= float( discret_build_pos_[0] + 1.0f / 3.0f ) * H_SPACE_SCALE_VECTOR_X;
	build_pos_.y= float( discret_build_pos_[1] ) - 0.5f * float(discret_build_pos_[0]&1) + 0.5f;
	build_pos_.z= float( discret_build_pos_[2] ) - 1.0f;
}

void h_Player::Build()
{
	if( build_block_ != h_BlockType::Unknown && build_direction_ != h_Direction::Unknown )
	{
		world_->AddBuildEvent(
			discret_build_pos_[0] - world_->Longitude() * H_CHUNK_WIDTH,
			discret_build_pos_[1] - world_->Latitude () * H_CHUNK_WIDTH,
			discret_build_pos_[2], build_block_ );
	}
}

void h_Player::Dig()
{
	if( build_direction_ != h_Direction::Unknown )
	{
		short dig_pos[3]=
		{
			discret_build_pos_[0],
			discret_build_pos_[1],
			discret_build_pos_[2]
		};

		switch( build_direction_ )
		{
		case h_Direction::Up:
			dig_pos[2]--;
			break;
		case h_Direction::Down:
			dig_pos[2]++;
			break;

		case h_Direction::Forward:
			dig_pos[1]--;
			break;
		case h_Direction::Back:
			dig_pos[1]++;
			break;

		case h_Direction::ForwardRight:
			dig_pos[1]-= (dig_pos[0]&1);
			dig_pos[0]--;
			break;
		case h_Direction::BackRight:
			dig_pos[1]+= ((dig_pos[0]+1)&1);
			dig_pos[0]--;
			break;

		case h_Direction::ForwardLeft:
			dig_pos[1]-= (dig_pos[0]&1);
			dig_pos[0]++;
			break;
		case h_Direction::BackLeft:
			dig_pos[1]+= ((dig_pos[0]+1)&1);
			dig_pos[0]++;
			break;

		default: break;
		};

		world_->AddDestroyEvent(
			dig_pos[0] - world_->Longitude() * H_CHUNK_WIDTH,
			dig_pos[1] - world_->Latitude () * H_CHUNK_WIDTH,
			dig_pos[2] );
	}
}

void h_Player::TestMobSetPosition()
{
	if( build_direction_ != h_Direction::Unknown )
	{
		world_->TestMobSetTargetPosition( discret_build_pos_[0], discret_build_pos_[1], discret_build_pos_[2] );
	}
}

void h_Player::UpdateBuildPos()
{
	m_Vec3 eye_dir(
		-std::sin( view_angle_.z ) * std::cos( view_angle_.x ),
		+std::cos( view_angle_.z ) * std::cos( view_angle_.x ),
		+std::sin( view_angle_.x ) );

	m_Vec3 eye_pos= pos_;
	eye_pos.z+= H_PLAYER_EYE_LEVEL;
	float dst= std::numeric_limits<float>::max();
	h_Direction block_dir= h_Direction::Unknown;

	m_Vec3 intersect_pos;
	m_Vec3 candidate_pos;
	m_Vec3 triangle[3];
	m_Vec3 n;

	for( const p_UpperBlockFace& face : phys_mesh_.upper_block_faces )
	{
		n= g_block_normals[ static_cast<size_t>(face.dir) ];

		triangle[0]= m_Vec3( face.edge[0].x, face.edge[0].y, face.z );
		triangle[1]= m_Vec3( face.edge[1].x, face.edge[1].y, face.z );
		triangle[2]= m_Vec3( face.edge[2].x, face.edge[2].y, face.z );
		if( RayHasIniersectWithTriangle( triangle, n, eye_pos, eye_dir, &candidate_pos ) )
		{
			float candidate_dst= ( candidate_pos - eye_pos ).Length();
			if( candidate_dst < dst )
			{
				dst= candidate_dst;
				intersect_pos= candidate_pos;
				block_dir= face.dir;
			}
		}

		triangle[0]= m_Vec3( face.edge[2].x, face.edge[2].y, face.z );
		triangle[1]= m_Vec3( face.edge[3].x, face.edge[3].y, face.z );
		triangle[2]= m_Vec3( face.edge[4].x, face.edge[4].y, face.z );
		if( RayHasIniersectWithTriangle( triangle, n, eye_pos, eye_dir, &candidate_pos ) )
		{
			float candidate_dst= ( candidate_pos - eye_pos ).Length();
			if( candidate_dst < dst )
			{
				dst= candidate_dst;
				intersect_pos= candidate_pos;
				block_dir= face.dir;
			}
		}

		triangle[0]= m_Vec3( face.edge[4].x, face.edge[4].y, face.z );
		triangle[1]= m_Vec3( face.edge[5].x, face.edge[5].y, face.z );
		triangle[2]= m_Vec3( face.edge[0].x, face.edge[0].y, face.z );
		if( RayHasIniersectWithTriangle( triangle, n, eye_pos, eye_dir, &candidate_pos ) )
		{
			float candidate_dst= ( candidate_pos - eye_pos ).Length();
			if( candidate_dst < dst )
			{
				dst= candidate_dst;
				intersect_pos= candidate_pos;
				block_dir= face.dir;
			}
		}

		triangle[0]= m_Vec3( face.edge[0].x, face.edge[0].y, face.z );
		triangle[1]= m_Vec3( face.edge[2].x, face.edge[2].y, face.z );
		triangle[2]= m_Vec3( face.edge[4].x, face.edge[4].y, face.z );
		if( RayHasIniersectWithTriangle( triangle, n, eye_pos, eye_dir, &candidate_pos ) )
		{
			float candidate_dst= ( candidate_pos - eye_pos ).Length();
			if( candidate_dst < dst )
			{
				dst= candidate_dst;
				intersect_pos= candidate_pos;
				block_dir= face.dir;
			}
		}
	}

	for( const p_BlockSide& side : phys_mesh_.block_sides )
	{
		n= g_block_normals[ static_cast<size_t>(side.dir) ];

		triangle[0]= m_Vec3( side.edge[0].x, side.edge[0].y, side.z );
		triangle[1]= m_Vec3( side.edge[1].x, side.edge[1].y, side.z );
		triangle[2]= m_Vec3( side.edge[1].x, side.edge[1].y, side.z + 1.0f );
		if( RayHasIniersectWithTriangle( triangle, n, eye_pos, eye_dir, &candidate_pos ) )
		{
			float candidate_dst= ( candidate_pos - eye_pos ).Length();
			if( candidate_dst < dst )
			{
				dst= candidate_dst;
				intersect_pos= candidate_pos;
				block_dir= side.dir;
			}
		}

		triangle[0]= m_Vec3( side.edge[0].x, side.edge[0].y, side.z + 1.0f );
		triangle[1]= m_Vec3( side.edge[0].x, side.edge[0].y, side.z );
		triangle[2]= m_Vec3( side.edge[1].x, side.edge[1].y, side.z + 1.0f );
		if( RayHasIniersectWithTriangle( triangle, n, eye_pos, eye_dir, &candidate_pos ) )
		{
			float candidate_dst= ( candidate_pos - eye_pos ).Length();
			if( candidate_dst < dst )
			{
				dst= candidate_dst;
				intersect_pos= candidate_pos;
				block_dir= side.dir;
			}
		}
	}

	if( block_dir == h_Direction::Unknown ||
		(intersect_pos - eye_pos).SquareLength() > g_max_build_distance * g_max_build_distance )
	{
		build_direction_= h_Direction::Unknown;
		return;
	}

	// Fix accuracy.
	intersect_pos+= g_block_normals[ static_cast<size_t>(block_dir) ] * 0.1f;

	short new_x, new_y, new_z;
	GetHexogonCoord( intersect_pos.xy(), &new_x, &new_y );
	new_z= (short) intersect_pos.z;
	new_z++;

	discret_build_pos_[0]= new_x;
	discret_build_pos_[1]= new_y;
	discret_build_pos_[2]= new_z;

	build_direction_= block_dir;
}

void h_Player::MoveInternal( const m_Vec3& delta )
{
	const float c_eps= 0.00001f;
	const float c_vertical_collision_eps= 0.001f;
	const float c_on_ground_eps= 0.01f;

	m_Vec3 new_pos= pos_ + delta;

	for( const p_UpperBlockFace& face : phys_mesh_.upper_block_faces )
	{
		if( delta.z > c_eps )
		{
			if( face.dir == h_Direction::Down &&
				face.z >= (pos_.z + H_PLAYER_HEIGHT) &&
				face.z < (new_pos.z + H_PLAYER_HEIGHT) &&
				face.HasCollisionWithCircle( new_pos.xy(), H_PLAYER_RADIUS ) )
				{
					new_pos.z= face.z - H_PLAYER_HEIGHT - c_vertical_collision_eps;
					break;
				}
		}
		else if( delta.z < -c_eps )
		{
			if( face.dir == h_Direction::Up &&
				face.z <= pos_.z &&
				face.z > new_pos.z &&
				face.HasCollisionWithCircle( new_pos.xy(), H_PLAYER_RADIUS ) )
			{
				new_pos.z= face.z + c_vertical_collision_eps;
				break;
			}
		}
	}// upeer faces

	for( const p_BlockSide& side : phys_mesh_.block_sides )
	{
		if( ( side.z > new_pos.z && side.z < new_pos.z + H_PLAYER_HEIGHT ) ||
			( side.z + 1.0f > new_pos.z && side.z + 1.0f < new_pos.z + H_PLAYER_HEIGHT ) )
		{
			m_Vec2 collide_pos= side.CollideWithCirlce( new_pos.xy(), H_PLAYER_RADIUS );
			if( collide_pos != new_pos.xy() )
			{
				new_pos.x= collide_pos.x;
				new_pos.y= collide_pos.y;

				// Zero speed component, perpendicalar to this side.
				const m_Vec3& normal= g_block_normals[ static_cast<size_t>(side.dir) ];
				speed_-= ( speed_ * normal ) * normal;
			}
		}
	}

	// Check in_air
	in_air_= true;
	for( const p_UpperBlockFace& face : phys_mesh_.upper_block_faces )
	{
		if( face.dir == h_Direction::Up &&
			new_pos.z <= face.z + c_on_ground_eps &&
			new_pos.z > face.z &&
			face.HasCollisionWithCircle( new_pos.xy(), H_PLAYER_RADIUS ) )
		{
			in_air_= false;
			vertical_speed_= 0.0f;
			break;
		}
		if( face.dir == h_Direction::Down &&
			new_pos.z + H_PLAYER_HEIGHT >= face.z - c_on_ground_eps &&
			new_pos.z + H_PLAYER_HEIGHT < face.z &&
			face.HasCollisionWithCircle( new_pos.xy(), H_PLAYER_RADIUS ) )
		{
			vertical_speed_= 0.0f;
			break;
		}
	}

	pos_= new_pos;
}
