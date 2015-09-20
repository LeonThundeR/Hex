#include "../world.hpp"

#include "world_renderer.hpp"
#include "texture_manager.hpp"
#include "rendering_constants.hpp"

/*
void r_WaterQuadChunkInfo::GetVertexCount()
{
	new_vertex_count_= 0;
	for( unsigned int i= 0; i< 2; i++ )
		for( unsigned int j= 0; j< 2; j++ )
		{
			if( chunks_[i][j] != nullptr )
				new_vertex_count_+= chunks_[i][j]->water_surface_mesh_vertices_.Size();
		}
}

void r_WaterQuadChunkInfo::GetUpdatedState()
{
	water_updated_= false;
	for( unsigned int i= 0; i< 2; i++ )
		for( unsigned int j= 0; j< 2; j++ )
			if( chunks_[i][j] != nullptr )
				water_updated_= water_updated_ || chunks_[i][j]->chunk_water_data_updated_;
}

void r_WaterQuadChunkInfo::BuildFinalMesh()
{
	r_WaterVertex* v= vb_data_;
	unsigned int s;

	for( unsigned int i= 0; i< 2; i++ )
		for( unsigned int j= 0; j< 2; j++ )
		{
			if( chunks_[i][j] != nullptr )
			{
				s= chunks_[i][j]->water_surface_mesh_vertices_.Size();
				memcpy( v, chunks_[i][j]->water_surface_mesh_vertices_.Data(), s * sizeof( r_WaterVertex ) );
				v+= s;
			}
		}

	real_vertex_count_= new_vertex_count_;
}
*/

r_ChunkInfo::r_ChunkInfo()
	: chunk_front_(nullptr), chunk_right_(nullptr)
	, chunk_back_right_(nullptr), chunk_back_(nullptr)
{
}

void r_ChunkInfo::GetWaterHexCount()
{
	auto water_block_list= chunk_->GetWaterList();
	m_Collection< h_LiquidBlock* >::ConstIterator iter( water_block_list );

	unsigned int hex_count= 0;

	for( iter.Begin(); iter.IsValid(); iter.Next() )
	{
		const h_LiquidBlock* b= *iter;
		h_BlockType type= chunk_->GetBlock( b->x_, b->y_, b->z_ + 1 )->Type();
		if( type == AIR || ( b->LiquidLevel() < H_MAX_WATER_LEVEL && type != WATER ) )
			hex_count++;
	}

	water_vertex_count_= hex_count * 6;
}

void r_ChunkInfo::BuildWaterSurfaceMesh()
{
	auto water_block_list= chunk_->GetWaterList();
	m_Collection< h_LiquidBlock* >::ConstIterator iter( water_block_list );
	const h_LiquidBlock* b;
	r_WaterVertex* v, *sv;
	short h;
	const h_Chunk* ch;

	v= water_vertex_data_;

	short X= chunk_->Longitude() * H_CHUNK_WIDTH;
	short Y= chunk_->Latitude() * H_CHUNK_WIDTH;
	short chunk_loaded_zone_X;
	short chunk_loaded_zone_Y;

	unsigned int vertex_water_level[6];
	unsigned short vertex_water_block_count[6];
	const h_World* world= chunk_->GetWorld();
	short global_x, global_y;
	short nearby_block_x, nearby_block_y;

	chunk_loaded_zone_X= ( chunk_->Longitude() - world->Longitude() ) * H_CHUNK_WIDTH;
	chunk_loaded_zone_Y= ( chunk_->Latitude() - world->Latitude() ) * H_CHUNK_WIDTH;
	if( ! chunk_->IsEdgeChunk() )
	{
		for( iter.Begin(); iter.IsValid(); iter.Next() )
		{
			b= *iter;

			h_BlockType type= chunk_->GetBlock( b->x_, b->y_, b->z_ + 1 )->Type();
			if( type == AIR || ( b->LiquidLevel() < H_MAX_WATER_LEVEL && type != WATER ) )
			{
				v[0].coord[0]= 3 * ( b->x_ + X );
				v[1].coord[0]= v[5].coord[0]= v[0].coord[0] + 1;
				v[2].coord[0]= v[4].coord[0]= v[0].coord[0] + 3;
				v[3].coord[0]= v[0].coord[0] + 4;

				v[0].coord[1]= v[3].coord[1]= 2 * ( b->y_ + Y ) - ((b->x_)&1) + 2;
				v[1].coord[1]= v[2].coord[1]= v[0].coord[1] + 1;
				v[4].coord[1]= v[5].coord[1]= v[0].coord[1] - 1;

				//calculate height of vertices
				const h_Block* b2, *b3;
				bool upper_block_is_water[6]= { false, false, false, false, false, false };
				bool nearby_block_is_air[6]= { false, false, false, false, false, false };
				vertex_water_level[0]= vertex_water_level[1]= vertex_water_level[2]=
				vertex_water_level[3]= vertex_water_level[4]= vertex_water_level[5]= b->LiquidLevel();
				vertex_water_block_count[0]= vertex_water_block_count[1]= vertex_water_block_count[2]=
				vertex_water_block_count[3]= vertex_water_block_count[4]= vertex_water_block_count[5]= 1;

				//forward
				global_x= b->x_ + chunk_loaded_zone_X;
				global_y= b->y_ + chunk_loaded_zone_Y + 1;
				nearby_block_x= (global_x)&( H_CHUNK_WIDTH-1);
				nearby_block_y= (global_y)&( H_CHUNK_WIDTH-1);
				b2= ( ch= world->GetChunk( global_x >> H_CHUNK_WIDTH_LOG2,
										   global_y >> H_CHUNK_WIDTH_LOG2 )
					)-> GetBlock( nearby_block_x, nearby_block_y, b->z_ );
				b3= ch->GetBlock( nearby_block_x, nearby_block_y, b->z_ + 1 );
				if( b3->Type() == WATER )
					upper_block_is_water[1]= upper_block_is_water[2]= true;
				else if( b2->Type() == AIR )
					nearby_block_is_air[1]= nearby_block_is_air[2]= true;
				else if( b2->Type() == WATER )
				{
					vertex_water_level[1]+= ((h_LiquidBlock*)b2)->LiquidLevel();
					vertex_water_level[2]+= ((h_LiquidBlock*)b2)->LiquidLevel();
					vertex_water_block_count[1]++;
					vertex_water_block_count[2]++;
				}

				//back
				global_x= b->x_ + chunk_loaded_zone_X;
				global_y= b->y_ + chunk_loaded_zone_Y - 1;
				nearby_block_x= (global_x)&( H_CHUNK_WIDTH-1);
				nearby_block_y= (global_y)&( H_CHUNK_WIDTH-1);
				b2= ( ch= world->GetChunk( global_x >> H_CHUNK_WIDTH_LOG2,
										   global_y >> H_CHUNK_WIDTH_LOG2 )
					)-> GetBlock( nearby_block_x, nearby_block_y, b->z_ );
				b3= ch->GetBlock( nearby_block_x, nearby_block_y, b->z_ + 1 );
				if( b3->Type() == WATER )
					upper_block_is_water[4]= upper_block_is_water[5]= true;
				else if( b2->Type() == AIR )
					nearby_block_is_air[4]= nearby_block_is_air[5]= true;
				else if( b2->Type() == WATER )
				{
					vertex_water_level[4]+= ((h_LiquidBlock*)b2)->LiquidLevel();
					vertex_water_level[5]+= ((h_LiquidBlock*)b2)->LiquidLevel();
					vertex_water_block_count[4]++;
					vertex_water_block_count[5]++;
				}

				//forward right
				global_x= b->x_ + chunk_loaded_zone_X + 1;
				global_y= b->y_ + chunk_loaded_zone_Y +((b->x_+1)&1);
				nearby_block_x= (global_x)&( H_CHUNK_WIDTH-1);
				nearby_block_y= (global_y)&( H_CHUNK_WIDTH-1);
				b2= ( ch= world->GetChunk( global_x >> H_CHUNK_WIDTH_LOG2,
										   global_y >> H_CHUNK_WIDTH_LOG2 )
					)-> GetBlock( nearby_block_x, nearby_block_y, b->z_ );
				b3= ch->GetBlock( nearby_block_x, nearby_block_y, b->z_ + 1 );
				if( b3->Type() == WATER )
					upper_block_is_water[2]= upper_block_is_water[3]= true;
				else if( b2->Type() == AIR )
					nearby_block_is_air[2]= nearby_block_is_air[3]= true;
				else if( b2->Type() == WATER )
				{
					vertex_water_level[2]+= ((h_LiquidBlock*)b2)->LiquidLevel();
					vertex_water_level[3]+= ((h_LiquidBlock*)b2)->LiquidLevel();
					vertex_water_block_count[2]++;
					vertex_water_block_count[3]++;
				}

				//back left
				global_x= b->x_ + chunk_loaded_zone_X - 1;
				global_y= b->y_ + chunk_loaded_zone_Y - (b->x_&1);
				nearby_block_x= (global_x)&( H_CHUNK_WIDTH-1);
				nearby_block_y= (global_y)&( H_CHUNK_WIDTH-1);
				b2= ( ch= world->GetChunk( global_x >> H_CHUNK_WIDTH_LOG2,
										   global_y >> H_CHUNK_WIDTH_LOG2 )
					)-> GetBlock( nearby_block_x, nearby_block_y, b->z_ );
				b3= ch->GetBlock( nearby_block_x, nearby_block_y, b->z_ + 1 );
				if( b3->Type() == WATER )
					upper_block_is_water[0]= upper_block_is_water[5]= true;
				else if( b2->Type() == AIR )
					nearby_block_is_air[0]= nearby_block_is_air[5]= true;
				else if( b2->Type() == WATER )
				{
					vertex_water_level[0]+= ((h_LiquidBlock*)b2)->LiquidLevel();
					vertex_water_level[5]+= ((h_LiquidBlock*)b2)->LiquidLevel();
					vertex_water_block_count[0]++;
					vertex_water_block_count[5]++;
				}

				//back right
				global_x= b->x_ + chunk_loaded_zone_X + 1;
				global_y= b->y_ + chunk_loaded_zone_Y -(b->x_&1);
				nearby_block_x= (global_x)&( H_CHUNK_WIDTH-1);
				nearby_block_y= (global_y)&( H_CHUNK_WIDTH-1);
				b2= ( ch= world->GetChunk( global_x >> H_CHUNK_WIDTH_LOG2,
										   global_y >> H_CHUNK_WIDTH_LOG2 )
					)-> GetBlock( nearby_block_x, nearby_block_y, b->z_ );
				b3= ch->GetBlock( nearby_block_x, nearby_block_y, b->z_ + 1 );
				if( b3->Type() == WATER )
					upper_block_is_water[3]= upper_block_is_water[4]= true;
				else if( b2->Type() == AIR )
					nearby_block_is_air[3]= nearby_block_is_air[4]= true;
				else if( b2->Type() == WATER )
				{
					vertex_water_level[3]+= ((h_LiquidBlock*)b2)->LiquidLevel();
					vertex_water_level[4]+= ((h_LiquidBlock*)b2)->LiquidLevel();
					vertex_water_block_count[3]++;
					vertex_water_block_count[4]++;
				}
				//forward left
				global_x= b->x_ + chunk_loaded_zone_X - 1;
				global_y= b->y_ + chunk_loaded_zone_Y + ((b->x_+1)&1);
				nearby_block_x= (global_x)&( H_CHUNK_WIDTH-1);
				nearby_block_y= (global_y)&( H_CHUNK_WIDTH-1);
				b2= ( ch= world->GetChunk( global_x >> H_CHUNK_WIDTH_LOG2,
										   global_y >> H_CHUNK_WIDTH_LOG2 )
					)-> GetBlock( nearby_block_x, nearby_block_y, b->z_ );
				b3= ch->GetBlock( nearby_block_x, nearby_block_y, b->z_ + 1 );
				if( b3->Type() == WATER )
					upper_block_is_water[0]= upper_block_is_water[1]= true;
				else if( b2->Type() == AIR )
					nearby_block_is_air[0]= nearby_block_is_air[1]= true;
				else if( b2->Type() == WATER )
				{
					vertex_water_level[0]+= ((h_LiquidBlock*)b2)->LiquidLevel();
					vertex_water_level[1]+= ((h_LiquidBlock*)b2)->LiquidLevel();
					vertex_water_block_count[0]++;
					vertex_water_block_count[1]++;
				}

				for( unsigned int k= 0; k< 6; k++ )
				{
#define DIV_TABLE_SCALER 16384
#define DIV_TABLE_SCALER_LOG2 14
					static const unsigned int div_table[]= { 0, DIV_TABLE_SCALER/1, DIV_TABLE_SCALER/2, DIV_TABLE_SCALER/3, DIV_TABLE_SCALER/4, DIV_TABLE_SCALER/5, DIV_TABLE_SCALER/6 };//for faster division ( but not precise )
					if( upper_block_is_water[k] )
						v[k].coord[2]= b->z_<<R_WATER_VERTICES_Z_SCALER_LOG2;
					else if( nearby_block_is_air[k] )
						v[k].coord[2]= (b->z_-1)<<R_WATER_VERTICES_Z_SCALER_LOG2;
					else
					{
						v[k].coord[2]=
							((b->z_-1)<<R_WATER_VERTICES_Z_SCALER_LOG2) +
							( ( vertex_water_level[k] * div_table[vertex_water_block_count[k]] ) >> (  H_MAX_WATER_LEVEL_LOG2 + DIV_TABLE_SCALER_LOG2 - R_WATER_VERTICES_Z_SCALER_LOG2 ) );
					}
				}
				world->GetForwardVertexLight( b->x_ + chunk_loaded_zone_X - 1, b->y_ + chunk_loaded_zone_Y - (b->x_&1), b->z_, v[0].light );
				world->GetBackVertexLight( b->x_ + chunk_loaded_zone_X, b->y_ + chunk_loaded_zone_Y + 1, b->z_, v[1].light );
				world->GetForwardVertexLight( b->x_ + chunk_loaded_zone_X, b->y_ + chunk_loaded_zone_Y, b->z_, v[2].light );
				world->GetBackVertexLight( b->x_ + chunk_loaded_zone_X + 1, b->y_ + chunk_loaded_zone_Y + ((1+b->x_)&1), b->z_, v[3].light );
				world->GetForwardVertexLight( b->x_ + chunk_loaded_zone_X, b->y_ + chunk_loaded_zone_Y - 1, b->z_, v[4].light );
				world->GetBackVertexLight(  b->x_ + chunk_loaded_zone_X, b->y_ + chunk_loaded_zone_Y, b->z_, v[5].light );

				v+= 6;
			}
		}

	}//smooth water surface
	else
	{
		for( iter.Begin(); iter.IsValid(); iter.Next() )
		{
			b= *iter;

			h_BlockType type= chunk_->GetBlock( b->x_, b->y_, b->z_ + 1 )->Type();
			if( type  == AIR || ( b->LiquidLevel() < H_MAX_WATER_LEVEL && type != WATER ) )
			{
				v[0].coord[0]= 3 * ( b->x_ + X );
				v[1].coord[0]= v[5].coord[0]= v[0].coord[0] + 1;
				v[2].coord[0]= v[4].coord[0]= v[0].coord[0] + 3;
				v[3].coord[0]= v[0].coord[0] + 4;

				v[0].coord[1]= v[3].coord[1]= 2 * ( b->y_ + Y ) - ((b->x_)&1) + 2;
				v[1].coord[1]= v[2].coord[1]= v[0].coord[1] + 1;
				v[4].coord[1]= v[5].coord[1]= v[0].coord[1] - 1;

				h= ( (b->z_ -1) << R_WATER_VERTICES_Z_SCALER_LOG2 )
					+ ( b->LiquidLevel() >> ( H_MAX_WATER_LEVEL_LOG2 - R_WATER_VERTICES_Z_SCALER_LOG2 ) );
				v[0].coord[2]= v[1].coord[2]= v[2].coord[2]= v[3].coord[2]= v[4].coord[2]= v[5].coord[2]= h;

				v[0].light[0]= v[1].light[0]= v[2].light[0]=
				v[3].light[0]= v[4].light[0]= v[5].light[0]=
					chunk_->SunLightLevel( b->x_, b->y_, b->z_ + 1 ) << 4;

				v[0].light[1]= v[1].light[1]= v[2].light[1]=
				v[3].light[1]= v[4].light[1]= v[5].light[1]=
					chunk_->FireLightLevel( b->x_, b->y_, b->z_ + 1 ) << 4;
				v+= 6;
			}// if water surface
		}//for
	}
}

/*
 __    __
/ @\__/ @\__
\__/ @\__/ &\
/ d\__/ d\__/
\__/ d\__/ =\
/ d\__/ d\__/
\__/ d\__/ =\
/ *\__/ *\__/
\__/ *\__/ +\
   \__/  \__/

 f
_____
     \ fr
  up  \
      /
     / br

d - default stage
@ - front edge
* - back edge
= - right edge
&,+ - corners
*/

void r_ChunkInfo::GetQuadCount()
{
	short x, y, z;

	unsigned char t, t_up, t_fr, t_br, t_f;

	unsigned int quad_count= 0;

	min_geometry_height_= H_CHUNK_HEIGHT;
	max_geometry_height_= 0;

	for( x= 0; x< H_CHUNK_WIDTH; x++ )
		for( y= 0; y< H_CHUNK_WIDTH; y++ )
		{
			t_up= chunk_->Transparency( x, y, 0 );

			const unsigned char* t_up_p= chunk_->GetTransparencyData() + BlockAddr(x,y,1);
			const unsigned char* t_fr_p= chunk_->GetTransparencyData() + BlockAddr(x + 1, y + (1&(x+1)),0);
			const unsigned char* t_br_p= chunk_->GetTransparencyData() + BlockAddr(x + 1, y - ( 1&x )  ,0);
			const unsigned char* t_f_p=  chunk_->GetTransparencyData() + BlockAddr(x,y+1,0);

			//front chunk border
			if( y == H_CHUNK_WIDTH - 1 && x < H_CHUNK_WIDTH - 1 )
			{
				if( chunk_front_ != nullptr )
					t_f_p= chunk_front_->GetTransparencyData() + BlockAddr( x, 0, 0 );
				else
					t_f_p= chunk_->GetTransparencyData() + BlockAddr(x,H_CHUNK_WIDTH - 1,0);//this block transparency
				if(x&1)
					t_fr_p= chunk_->GetTransparencyData() + BlockAddr( x + 1, H_CHUNK_WIDTH - 1, 0 );
				else if( chunk_front_ != nullptr )
					t_fr_p= chunk_front_->GetTransparencyData() + BlockAddr( x + 1, 0, 0 );
				else
					t_fr_p= chunk_->GetTransparencyData() + BlockAddr(x,H_CHUNK_WIDTH - 1,0);//this block transparency
			}
			//back chunk border
			if( y == 0 && x < H_CHUNK_WIDTH - 1 )
			{
				if(!(x&1))
					t_br_p= chunk_->GetTransparencyData() + BlockAddr( x + 1, 0, 0 );
				else if( chunk_back_ != nullptr )
					t_br_p= chunk_back_->GetTransparencyData() + BlockAddr( x+ 1, H_CHUNK_WIDTH - 1, 0 );
				else
					t_br_p= chunk_->GetTransparencyData() + BlockAddr(x,0,0);//this block transparency
			}
			//right chunk border
			if( x == H_CHUNK_WIDTH - 1 && y> 0 && y< H_CHUNK_WIDTH-1 )
			{
				if( chunk_right_ != nullptr )
				{
					t_fr_p= chunk_right_->GetTransparencyData() + BlockAddr( 0, y, 0 );
					t_br_p= chunk_right_->GetTransparencyData() + BlockAddr( 0, y - 1, 0 );
				}
				else
					t_fr_p= t_br_p= chunk_->GetTransparencyData() + BlockAddr(H_CHUNK_WIDTH - 1,y,0);//this block transparency
			}
			if( x == H_CHUNK_WIDTH - 1 && y == H_CHUNK_WIDTH - 1 )
			{
				if( chunk_front_ != nullptr )
					t_f_p= chunk_front_->GetTransparencyData() + BlockAddr( H_CHUNK_WIDTH - 1, 0, 0 );
				else
					t_f_p= chunk_->GetTransparencyData() + BlockAddr(x,y,0);//this block transparency;
				if( chunk_right_ != nullptr )
				{
					t_fr_p= chunk_right_->GetTransparencyData() + BlockAddr( 0, H_CHUNK_WIDTH  - 1, 0 );
					t_br_p= chunk_right_->GetTransparencyData() + BlockAddr( 0, H_CHUNK_WIDTH  - 2, 0 );
				}
				else
					t_fr_p= t_br_p= chunk_->GetTransparencyData() + BlockAddr(H_CHUNK_WIDTH - 1,H_CHUNK_WIDTH - 1,0);//this block transparency;
			}
			if( x == H_CHUNK_WIDTH - 1 && y == 0 )
			{
				//t_f_p= chunk->GetTransparencyData() + BlockAddr( H_CHUNK_WIDTH - 1, 1, 0 );
				if( chunk_right_ != nullptr )
					t_fr_p= chunk_right_->GetTransparencyData() + BlockAddr( 0, 0, 0 );
				else
					t_fr_p= chunk_->GetTransparencyData() + BlockAddr(H_CHUNK_WIDTH - 1,0,0);//this block transparency;
				if( chunk_back_right_ != nullptr )
					t_br_p= chunk_back_right_->GetTransparencyData() + BlockAddr( 0, H_CHUNK_WIDTH - 1, 0 );
				else
					t_br_p= chunk_->GetTransparencyData() + BlockAddr(H_CHUNK_WIDTH - 1,0,0);//this block transparency;
			}
			for( z= 0; z< H_CHUNK_HEIGHT - 2; z++ )
			{
				t= t_up;
				t_fr= *t_fr_p;
				t_br= *t_br_p;
				t_up= *t_up_p;
				t_f= *t_f_p;

#define ADD_QUADS \
                if( t != t_up ) \
                {\
                    quad_count+=2; \
                    if( z > max_geometry_height_ ) max_geometry_height_= z;\
                    else if( z < min_geometry_height_ ) min_geometry_height_= z;\
                }\
                if( t != t_fr ) \
				{\
                    quad_count++; \
                    if( z > max_geometry_height_ ) max_geometry_height_= z;\
                    else if( z < min_geometry_height_ ) min_geometry_height_= z;\
                }\
                if( t != t_br ) \
				{\
                    quad_count++; \
                    if( z > max_geometry_height_ ) max_geometry_height_= z;\
                    else if( z < min_geometry_height_ ) min_geometry_height_= z;\
                }\
                if( t!= t_f ) \
				{\
                    quad_count++; \
                    if( z > max_geometry_height_ ) max_geometry_height_= z;\
                    else if( z < min_geometry_height_ ) min_geometry_height_= z;\
                }
				ADD_QUADS

				t_fr_p++;
				t_br_p++;
				t_up_p++;
				t_f_p++;

			}//for z
		}//for y

	goto func_end;

	//back chunk border( x E [ 0; H_CHUNK_WIDTH - 2 ], y=0 )
	for( x= 0; x< H_CHUNK_WIDTH - 1; x++ )
	{
		t_up= chunk_->Transparency(  x, 0, 0 );
		for( z= 0; z< H_CHUNK_HEIGHT - 2; z++ )
		{
			t= t_up;
			if( ! (x&1) )
				t_br= chunk_->Transparency( x + 1, 0, z );//forward right
			else if( chunk_back_ !=nullptr )
				t_br= chunk_back_->Transparency( x+ 1, H_CHUNK_WIDTH - 1, z );
			else
				t_br= t;
			t_fr= chunk_->Transparency( x + 1, ( 1&(x+1) ), z );//forward right
			t_up= chunk_->Transparency( x, 0, z + 1 );//up
			t_f= chunk_->Transparency( x, 1, z );//forward
			ADD_QUADS
		}
	}

	//right chunk border ( y E [ 1; H_CHUNK_WIDTH - 2 ] )
	for( y= 1; y< H_CHUNK_WIDTH - 1; y++ )
	{
		t_up= chunk_->Transparency(  H_CHUNK_WIDTH - 1, y, 0 );
		for( z= 0; z< H_CHUNK_HEIGHT - 2; z++ )
		{
			t= t_up;
			if( chunk_right_ != nullptr )
			{
				t_fr= chunk_right_->Transparency( 0, y, z );//forward right
				t_br= chunk_right_->Transparency( 0, y - 1, z );	//back right
			}
			else
				t_fr= t_br= t;
			t_up= chunk_->Transparency( H_CHUNK_WIDTH - 1, y, z + 1 );//up
			t_f= chunk_->Transparency( H_CHUNK_WIDTH - 1, y + 1, z );//forward
			ADD_QUADS
		}
	}

	//front chunk border ( x E[ 0; H_CHUNK_WIDTH - 2 ] )
	for( x= 0; x < H_CHUNK_WIDTH - 1; x++ )
	{
		t_up= chunk_->Transparency( x, H_CHUNK_WIDTH - 1, 0 );
		for( z= 0; z< H_CHUNK_HEIGHT - 2; z++ )
		{
			t= t_up;
			if( x&1  )
				t_fr= chunk_->Transparency( x + 1, H_CHUNK_WIDTH - 1, z );//forward right
			else if( chunk_front_ != nullptr )
				t_fr= chunk_front_->Transparency( x + 1, 0, z );//forward right
			else t_fr= t;

			t_br= chunk_->Transparency( x + 1, H_CHUNK_WIDTH - 1 - ( 1&x ), z );//back right
			t_up= chunk_->Transparency( x, H_CHUNK_WIDTH - 1, z + 1 );//up

			if( chunk_front_ != nullptr )
				t_f= chunk_front_->Transparency( x, 0, z );//forward
			else
				t_f= t;

			ADD_QUADS
		}
	}

	//x= y= H_CHUNK_WIDTH - 1
	t_up= chunk_->Transparency( H_CHUNK_WIDTH - 1, H_CHUNK_WIDTH - 1, 0 );
	for( z= 0; z< H_CHUNK_HEIGHT - 2; z++ )
	{
		t= t_up;
		if( chunk_front_ != nullptr )
			t_f= chunk_front_->Transparency( H_CHUNK_WIDTH - 1, 0, z );
		else
			t_f= t;

		if( chunk_right_ != nullptr )
		{
			t_fr= chunk_right_->Transparency( 0, H_CHUNK_WIDTH  - 1, z );
			t_br= chunk_right_->Transparency( 0, H_CHUNK_WIDTH  - 2, z );
		}
		else
			t_fr= t_br= t;
		t_up= chunk_->Transparency( H_CHUNK_WIDTH  - 1, H_CHUNK_WIDTH  - 1, z + 1 );//up
		ADD_QUADS

	}

	//x= H_CHUNK_WIDTH - 1, y=0
	t_up= chunk_->Transparency( H_CHUNK_WIDTH - 1, 0, 0 );
	for( z= 0; z< H_CHUNK_HEIGHT - 2; z++ )
	{
		t= t_up;
		t_f= chunk_->Transparency( H_CHUNK_WIDTH - 1, 1, z );

		if( chunk_right_ != nullptr )
			t_fr= chunk_right_->Transparency( 0, 0, z );
		else
			t_fr= t;

		if( chunk_back_right_ != nullptr )
			t_br= chunk_back_right_->Transparency( 0, H_CHUNK_WIDTH - 1, z );
		else
			t_br= t;
		t_up= chunk_->Transparency( H_CHUNK_WIDTH - 1, 0, z + 1 );//up
		ADD_QUADS
	}

func_end:
	vertex_count_= quad_count * 4;

#undef ADD_QUADS
}

/*
                       1____2
                       /    \
                    05/______\36
                      \      /
                      4\____/7
up/down side */

/*
                           2
                          /\
                     ____/  \3
                    /   1\  /
                   /______\/0
                   \      /
                    \____/
forward right side*/


/*
                     ____
                    /    \
                   /______\0
                   \      /\
                    \___3/  \1
                         \  /
                          \/
                           2
back right*/



/*
                       1+--+2 - z-1
                        |  |
                        |  |
                       0+__+3 - z
                       /    \
                      /______\
                      \      /
                       \____/
forward side*/

/* old code of tex_coord generation for upper prism side

v[0].tex_coord[0]= v[0].coord[0] & 127;\
                    v[1].tex_coord[0]= v[7].tex_coord[0]= v[0].tex_coord[0] + 1;\
                    v[2].tex_coord[0]= v[6].tex_coord[0]= v[0].tex_coord[0] + 3;\
                    v[3].tex_coord[0]= v[0].tex_coord[0] + 4;\
\
                    v[0].tex_coord[1]= v[3].tex_coord[1]= ( v[0].coord[1] & 127 ) + 1;\
                    v[1].tex_coord[1]= v[2].tex_coord[1]= v[0].tex_coord[1] + 1;\
                    v[6].tex_coord[1]= v[7].tex_coord[1]= v[0].tex_coord[1] - 1;\
*/
/* new code
					v[0].tex_coord[0]= 0;\
                    v[1].tex_coord[0]= v[7].tex_coord[0]= 1;\
                    v[2].tex_coord[0]= v[6].tex_coord[0]= 3;\
                    v[3].tex_coord[0]= 4;\
\
                    v[0].tex_coord[1]= v[3].tex_coord[1]= 1;\
                    v[1].tex_coord[1]= v[2].tex_coord[1]= 2;\
                    v[6].tex_coord[1]= v[7].tex_coord[1]= 0;\

*/

void r_ChunkInfo::BuildChunkMesh()
{
	int x, y, z;

	unsigned char t, t_up, t_fr, t_br, t_f;//block transparency
	unsigned char normal_id;
	unsigned char tex_id, tex_scale, light[2];

	r_WorldVertex* v= vertex_data_;
	r_WorldVertex tmp_vertex;

	const h_Block* b;

	int X= chunk_->Longitude() * H_CHUNK_WIDTH, Y= chunk_->Latitude() * H_CHUNK_WIDTH;
	int relative_X, relative_Y;
	const h_World* w= chunk_->GetWorld();
	relative_X= ( chunk_->Longitude() - w->Longitude() ) * H_CHUNK_WIDTH;
	relative_Y= ( chunk_->Latitude() - w->Latitude() ) * H_CHUNK_WIDTH;


	bool flat_lighting= chunk_->IsEdgeChunk();

	for( x= 0; x< H_CHUNK_WIDTH - 1; x++ )
	{
		for( y= 1; y< H_CHUNK_WIDTH - 1; y++ )
		{
			t_up= chunk_->Transparency( x, y, min_geometry_height_ );

			const unsigned char* t_up_p= chunk_->GetTransparencyData() + BlockAddr(x,y,min_geometry_height_+1);
			const unsigned char* t_fr_p= chunk_->GetTransparencyData() + BlockAddr(x + 1, y + (1&(x+1)),min_geometry_height_);
			const unsigned char* t_br_p= chunk_->GetTransparencyData() + BlockAddr(x + 1, y - ( 1&x )  ,min_geometry_height_);
			const unsigned char* t_f_p=  chunk_->GetTransparencyData() + BlockAddr(x,y+1,min_geometry_height_);

			for( z= min_geometry_height_; z<= max_geometry_height_; z++ )
			{
				t= t_up;
				t_fr= *t_fr_p;
				t_br= *t_br_p;
				t_up= *t_up_p;
				t_f= *t_f_p;

				if( t != t_up )//up
				{
#define BUILD_QUADS_UP \
                    if( t > t_up )\
                    {\
                        normal_id= DOWN;\
                        b= chunk_->GetBlock( x, y, z  + 1 );\
                        light[0]= chunk_->SunLightLevel( x, y, z );\
                        light[1]= chunk_->FireLightLevel( x, y, z );\
                    }\
                    else\
                    {\
                        normal_id= UP;\
                        b= chunk_->GetBlock( x, y, z  );\
                        light[0]= chunk_->SunLightLevel( x, y, z + 1 );\
                        light[1]= chunk_->FireLightLevel( x, y, z + 1 );\
                    }\
\
					\
					tex_id= r_TextureManager::GetTextureId( b->Type(), normal_id );\
					tex_scale= r_TextureManager::GetTextureScale( tex_id );\
					\
                    v[0].coord[0]= 3 * ( x + X );\
                    v[1].coord[0]= v[4].coord[0]= v[0].coord[0] + 1;\
                    v[2].coord[0]= v[7].coord[0]= v[0].coord[0] + 3;\
                    v[3].coord[0]= v[0].coord[0] + 4;\
\
                    v[0].coord[1]= v[3].coord[1]= 2 * ( y + Y ) - (x&1) + 2;\
                    v[1].coord[1]= v[2].coord[1]= v[0].coord[1] + 1;\
                    v[7].coord[1]= v[4].coord[1]= v[0].coord[1] - 1;\
\
                    v[0].coord[2]= v[1].coord[2]= v[2].coord[2]= v[3].coord[2]= v[7].coord[2]= v[4].coord[2]= z;\
\
					if( r_TextureManager::TexturePerBlock( tex_id ) )\
					{\
						static const short hex_tex_coord[]=\
						{ 0, H_MAX_TEXTURE_SCALE,  1, H_MAX_TEXTURE_SCALE*2,   3, H_MAX_TEXTURE_SCALE*2,\
						4, H_MAX_TEXTURE_SCALE,   3, 0,   1,0 };\
						unsigned int hex_rotation= 0;\
                    	v[0].tex_coord[0]= 0;\
                    	v[1].tex_coord[0]= v[4].tex_coord[0]= 1*H_MAX_TEXTURE_SCALE;\
                    	v[2].tex_coord[0]= v[7].tex_coord[0]= 3*H_MAX_TEXTURE_SCALE;\
                    	v[3].tex_coord[0]= 4*H_MAX_TEXTURE_SCALE;\
\
                    	v[0].tex_coord[1]= v[3].tex_coord[1]= 1*H_MAX_TEXTURE_SCALE;\
                    	v[1].tex_coord[1]= v[2].tex_coord[1]= 2*H_MAX_TEXTURE_SCALE;\
                    	v[7].tex_coord[1]= v[4].tex_coord[1]= 0;\
					}\
					else\
					{\
						v[0].tex_coord[0]= tex_scale * v[0].coord[0];\
                    	v[1].tex_coord[0]= v[4].tex_coord[0]= v[0].tex_coord[0] + 1*tex_scale;\
                    	v[2].tex_coord[0]= v[7].tex_coord[0]= v[0].tex_coord[0] + 3*tex_scale;\
                    	v[3].tex_coord[0]= v[0].tex_coord[0] + 4*tex_scale;\
\
                    	v[0].tex_coord[1]= v[3].tex_coord[1]= tex_scale * v[0].coord[1];\
                    	v[1].tex_coord[1]= v[2].tex_coord[1]= v[0].tex_coord[1] + 1*tex_scale;\
                    	v[7].tex_coord[1]= v[4].tex_coord[1]= v[0].tex_coord[1] - 1*tex_scale;\
					}\
\
                    v[0].normal_id= v[1].normal_id= v[2].normal_id= v[3].normal_id= v[7].normal_id= v[4].normal_id= normal_id;\
                    v[0].tex_coord[2]= v[1].tex_coord[2]= v[2].tex_coord[2]= v[3].tex_coord[2]= v[7].tex_coord[2]= v[4].tex_coord[2]= \
                    tex_id;\
                    if( flat_lighting )\
                    {\
                    	v[0].light[0]= v[1].light[0]= v[2].light[0]= v[3].light[0]= v[7].light[0]= v[4].light[0]= light[0] << 4;\
                    	v[0].light[1]= v[1].light[1]= v[2].light[1]= v[3].light[1]= v[7].light[1]= v[4].light[1]= light[1] << 4;\
                    }\
					else\
					{\
                    	w->GetForwardVertexLight( x + relative_X - 1, y + relative_Y - (x&1), z, v[0].light );\
                    	w->GetBackVertexLight( x + relative_X, y + relative_Y + 1, z, v[1].light );\
                    	w->GetForwardVertexLight( x + relative_X, y + relative_Y, z, v[2].light );\
                    	w->GetBackVertexLight( x + relative_X + 1, y + relative_Y + ((1+x)&1), z, v[3].light );\
                    	w->GetForwardVertexLight( x + relative_X, y + relative_Y - 1, z, v[7].light );\
                    	w->GetBackVertexLight(  x + relative_X, y + relative_Y, z, v[4].light );\
					}\
                    v[5]= v[0];\
                    v[6]= v[3];\
\
                    if( normal_id == DOWN )\
                    {\
                        tmp_vertex= v[1];\
                        v[1]= v[3];\
                        v[3]= tmp_vertex;\
\
                        tmp_vertex= v[5];\
                        v[5]= v[7];\
                        v[7]= tmp_vertex;\
                    }\
                    v+=8;\


					BUILD_QUADS_UP
				}

				if( t != t_fr )//forwaed right
				{
#define BUILD_QUADS_FORWARD_RIGHT \
\
					tex_id= r_TextureManager::GetTextureId( b->Type(), normal_id );\
					tex_scale= r_TextureManager::GetTextureScale( tex_id );\
\
                    v[ 1 ].coord[0]= v[2].coord[0]= 3 * ( x + X ) + 3;\
                    v[0].coord[0]= v[ 3 ].coord[0]= v[ 1 ].coord[0] + 1;\
\
                    v[0].coord[1]= v[ 3 ].coord[1]= 2 * ( y + Y ) - (x&1) + 2;\
                    v[ 1 ].coord[1]= v[2].coord[1]= v[0].coord[1] + 1;\
\
                    v[0].coord[2]= v[ 1 ].coord[2]= z;\
                    v[2].coord[2]= v[ 3 ].coord[2]= z - 1;\
\
\
                    v[ 1 ].tex_coord[0]= v[2].tex_coord[0]= tex_scale * ( v[ 1 ].coord[1] - v[1].coord[0] );\
                    v[0].tex_coord[0]= v[ 3 ].tex_coord[0]= v[ 1 ].tex_coord[0] - 2 * tex_scale;\
\
                    v[0].tex_coord[1]= v[ 1 ].tex_coord[1]= z * 2 * tex_scale;\
                    v[2].tex_coord[1]= v[ 3 ].tex_coord[1]= v[0].tex_coord[1] - 2 * tex_scale;\
\
                    v[0].tex_coord[2]= v[1].tex_coord[2]= v[2].tex_coord[2]= v[3].tex_coord[2]=\
                    tex_id;\
					if( flat_lighting )\
					{\
						v[0].light[1]= v[1].light[1]= v[2].light[1]= v[3].light[1]= light[1] << 4;\
						v[0].light[0]= v[1].light[0]= v[2].light[0]= v[3].light[0]= light[0] << 4;\
					}\
					else\
					{\
						w->GetBackVertexLight( x + relative_X + 1, y + relative_Y + ((x+1)&1), z, v[0].light );\
						w->GetForwardVertexLight( x + relative_X, y + relative_Y, z, v[1].light );\
						w->GetForwardVertexLight( x + relative_X, y + relative_Y, z-1, v[2].light  );\
						w->GetBackVertexLight( x + relative_X + 1, y + relative_Y + ((x+1)&1), z-1, v[3].light  );\
					}\
					v[0].normal_id= v[1].normal_id= v[2].normal_id= v[3].normal_id= normal_id;\
					if( normal_id == BACK_LEFT )\
					{\
						tmp_vertex= v[3];\
						v[3]= v[1];\
						v[1]= tmp_vertex;\
					}\
\
                    v+=4;
					if( t > t_fr )
					{
						normal_id= BACK_LEFT;
						b= chunk_->GetBlock( x + 1, y + ((x+1)&1), z );
						light[0]= chunk_->SunLightLevel( x, y, z );
						light[1]= chunk_->FireLightLevel( x, y, z );
					}
					else
					{
						normal_id= FORWARD_RIGHT;
						b= chunk_->GetBlock( x, y, z );
						light[0]= chunk_->SunLightLevel( x + 1, y + ((x+1)&1), z );
						light[1]= chunk_->FireLightLevel( x + 1, y + ((x+1)&1), z );
					}
					BUILD_QUADS_FORWARD_RIGHT
				}

				if( t != t_br )//back right
				{
#define BUILD_QUADS_BACK_RIGHT \
\
					tex_id= r_TextureManager::GetTextureId( b->Type(), normal_id );\
					tex_scale= r_TextureManager::GetTextureScale( tex_id );\
\
                    v[ 1 ].coord[0]= v[2].coord[0]= 3 * ( x + X ) + 3;\
                    v[0].coord[0]= v[ 3 ].coord[0]= v[ 1 ].coord[0] + 1;\
\
                    v[ 1 ].coord[1]= v[2].coord[1]= 2 * ( y + Y ) - (x&1) + 2 - 1;\
                    v[0].coord[1]= v[ 3 ].coord[1]= v[ 1 ].coord[1] + 1;\
\
                    v[ 1 ].coord[2]= v[0].coord[2]= z;\
                    v[2].coord[2]= v[ 3 ].coord[2]= z - 1;\
\
                   \
                    v[2].tex_coord[0]= v[ 1 ].tex_coord[0]=  ( v[1].coord[1]  + v[1].coord[0] ) * tex_scale;\
                    v[0].tex_coord[0]= v[ 3 ].tex_coord[0]= v[2].tex_coord[0] + 2 * tex_scale;\
\
                    v[0].tex_coord[1]= v[ 1 ].tex_coord[1]= z * 2 * tex_scale;\
                    v[ 3 ].tex_coord[1]= v[2].tex_coord[1]= v[0].tex_coord[1] - 2 * tex_scale;\
\
                    v[0].tex_coord[2]= v[1].tex_coord[2]= v[2].tex_coord[2]= v[3].tex_coord[2]=\
                    tex_id;\
					if( flat_lighting )\
					{\
						v[0].light[1]= v[1].light[1]= v[2].light[1]= v[3].light[1]= light[1] << 4;\
                    	v[0].light[0]= v[1].light[0]= v[2].light[0]= v[3].light[0]= light[0] << 4;\
					}\
					else\
					{\
						w->GetBackVertexLight( x + relative_X + 1, y + relative_Y + ((x+1)&1), z, v[0].light );\
						w->GetBackVertexLight( x + relative_X + 1, y + relative_Y + ((x+1)&1), z - 1, v[3].light );\
						w->GetForwardVertexLight( x + relative_X, y + relative_Y - 1, z - 1, v[2].light );\
						w->GetForwardVertexLight( x + relative_X, y + relative_Y - 1, z, v[1].light );\
					}\
                    v[0].normal_id= v[1].normal_id= v[2].normal_id= v[3].normal_id= normal_id;\
                    if( normal_id == BACK_RIGHT )\
                    {\
                    	tmp_vertex= v[3];\
						v[3]= v[1];\
						v[1]= tmp_vertex;\
                    }\
                    v+=4;
					if( t > t_br )
					{
						normal_id= FORWARD_LEFT;
						b= chunk_->GetBlock( x + 1, y - (x&1), z );
						light[0]= chunk_->SunLightLevel( x, y, z );
						light[1]= chunk_->FireLightLevel( x, y, z );
					}
					else
					{
						normal_id= BACK_RIGHT;
						b= chunk_->GetBlock( x, y, z );
						light[0]= chunk_->SunLightLevel( x + 1, y - (x&1), z );
						light[1]= chunk_->FireLightLevel( x + 1, y - (x&1), z );
					}
					BUILD_QUADS_BACK_RIGHT
				}

				if( t != t_f )//forward
				{
#define BUILD_QUADS_FORWARD\
\
					tex_id= r_TextureManager::GetTextureId( b->Type(), normal_id );\
					tex_scale= r_TextureManager::GetTextureScale( tex_id );\
\
                    v[0].coord[0]= v[ 1 ].coord[0]= 3 * ( x + X ) + 1;\
                    v[0].coord[1]= v[ 1 ].coord[1]= v[2].coord[1]= v[ 3 ].coord[1]= 2 * ( y + Y ) - (x&1) + 2 + 1;\
\
                    v[0].coord[2]= v[ 3 ].coord[2]= z;\
                    v[ 1 ].coord[2]= v[2].coord[2]= z - 1;\
\
                    v[ 3 ].coord[0]= v[2].coord[0]= v[ 1 ].coord[0] + 2;\
\
\
                    v[0].tex_coord[0]= v[ 1 ].tex_coord[0]= v[0].coord[0] * tex_scale;\
                    v[2].tex_coord[0]= v[ 3 ].tex_coord[0]= v[0].tex_coord[0] + 2 * tex_scale;\
\
                    v[0].tex_coord[1]= v[ 3 ].tex_coord[1]= z * 2 * tex_scale;\
                    v[ 1 ].tex_coord[1]= v[2].tex_coord[1]= v[0].tex_coord[1] - 2 * tex_scale;\
                    v[0].tex_coord[2]= v[1].tex_coord[2]= v[2].tex_coord[2]= v[3].tex_coord[2]=\
                    tex_id;\
                    \
                    if( flat_lighting )\
                    {\
                    	v[0].light[0]= v[1].light[0]= v[2].light[0]= v[3].light[0]= light[0] << 4;\
						v[0].light[1]= v[1].light[1]= v[2].light[1]= v[3].light[1]= light[1] << 4;\
                    }\
					else\
					{\
						w->GetBackVertexLight( x + relative_X, y + relative_Y + 1, z, v[0].light );\
						w->GetBackVertexLight( x + relative_X, y + relative_Y + 1, z - 1, v[1].light  );\
						w->GetForwardVertexLight( x + relative_X, y + relative_Y, z - 1, v[2].light  );\
						w->GetForwardVertexLight( x + relative_X, y + relative_Y, z, v[3].light  );\
					}\
                    v[0].normal_id= v[1].normal_id= v[2].normal_id= v[3].normal_id= normal_id;\
					if( normal_id == BACK )\
                    {\
                    	tmp_vertex= v[3];\
						v[3]= v[1];\
						v[1]= tmp_vertex;\
                    }\
                    v+= 4;
					if( t > t_f )
					{
						normal_id= BACK;
						b= chunk_->GetBlock( x, y + 1, z );
						light[0]= chunk_->SunLightLevel( x, y, z );
						light[1]= chunk_->FireLightLevel( x, y, z );
					}
					else
					{
						normal_id= FORWARD;
						b= chunk_->GetBlock( x, y, z );
						light[0]= chunk_->SunLightLevel( x, y+1, z );
						light[1]= chunk_->FireLightLevel( x, y+1, z );
					}
					BUILD_QUADS_FORWARD
				}//forward quad

				t_fr_p++;
				t_br_p++;
				t_up_p++;
				t_f_p++;
			}//for z
		}//for y
	}//for x

#if 1
	//back chunk border( x E [ 0; H_CHUNK_WIDTH - 2 ], y=0 )
	y= 0;
	for( x= 0; x< H_CHUNK_WIDTH - 1; x++ )
	{
		t_up= chunk_->Transparency(  x, 0, 0 );
		for( z= 0; z< H_CHUNK_HEIGHT - 2; z++ )
		{
			t= t_up;
			if( ! (x&1) )
				t_br= chunk_->Transparency( x + 1, 0, z );//back right
			else if( chunk_back_ != nullptr )
				t_br= chunk_back_->Transparency( x + 1, H_CHUNK_WIDTH - 1, z );
			else
				t_br= t;
			t_fr= chunk_->Transparency( x + 1, ( 1&(x+1) ), z );//forward right
			t_up= chunk_->Transparency( x, 0, z + 1 );//up
			t_f= chunk_->Transparency( x, 1, z );//forward

			if( t!= t_up )
			{
				BUILD_QUADS_UP
			}
			if( t!= t_fr )
			{
				if( t > t_fr )
				{
					normal_id= BACK_LEFT;
					b= chunk_->GetBlock( x + 1, y + ((x+1)&1), z );
					light[0]= chunk_->SunLightLevel( x, y, z );
					light[1]= chunk_->FireLightLevel( x, y, z );
				}
				else
				{
					normal_id= FORWARD_RIGHT;
					b= chunk_->GetBlock( x, y, z );
					light[0]= chunk_->SunLightLevel( x + 1, y + ((x+1)&1), z );
					light[1]= chunk_->FireLightLevel( x + 1, y + ((x+1)&1), z );
				}
				BUILD_QUADS_FORWARD_RIGHT
			}
			if( t!= t_br )
			{
				if( t > t_br )
				{
					normal_id= FORWARD_LEFT;
					b= (x&1) ? chunk_back_->GetBlock( x + 1, H_CHUNK_WIDTH - 1, z ) :
					   chunk_->GetBlock( x + 1, 0, z );
					light[0]= chunk_->SunLightLevel( x, y, z );
					light[1]= chunk_->FireLightLevel( x, y, z );
				}
				else
				{
					normal_id= BACK_RIGHT;
					b= chunk_->GetBlock( x, y, z );
					light[0]= (x&1) ? chunk_back_->SunLightLevel( x + 1, H_CHUNK_WIDTH - 1, z ) :
							  chunk_->SunLightLevel( x + 1, 0, z );

					light[1]= (x&1) ? chunk_back_->FireLightLevel( x + 1, H_CHUNK_WIDTH - 1, z ) :
							  chunk_->FireLightLevel( x + 1, 0, z );
				}
				BUILD_QUADS_BACK_RIGHT
			}
			if( t!= t_f )
			{
				if( t > t_f )
				{
					normal_id= BACK;
					b= chunk_->GetBlock( x, y + 1, z );
					light[0]= chunk_->SunLightLevel( x, y, z );
					light[1]= chunk_->FireLightLevel( x, y, z );
				}
				else
				{
					normal_id= FORWARD;
					b= chunk_->GetBlock( x, y, z );
					light[0]= chunk_->SunLightLevel( x, y + 1, z );
					light[1]= chunk_->FireLightLevel( x, y + 1, z );
				}
				BUILD_QUADS_FORWARD
			}

		}
	}
#endif
#if 1
	//right chunk border ( y E [ 1; H_CHUNK_WIDTH - 2 ] )
	x= H_CHUNK_WIDTH - 1;
	for( y= 1; y< H_CHUNK_WIDTH - 1; y++ )
	{
		t_up= chunk_->Transparency(  H_CHUNK_WIDTH - 1, y, 0 );
		for( z= 0; z< H_CHUNK_HEIGHT - 2; z++ )
		{
			t= t_up;
			if( chunk_right_ != nullptr )
			{
				t_fr= chunk_right_->Transparency( 0, y, z );//forward right
				t_br= chunk_right_->Transparency( 0, y - 1, z );	//back right
			}
			else
				t_fr= t_br= t;
			t_up= chunk_->Transparency( H_CHUNK_WIDTH - 1, y, z + 1 );//up
			t_f= chunk_->Transparency( H_CHUNK_WIDTH - 1, y + 1, z );//forward

			if( t!= t_up )
			{
				BUILD_QUADS_UP
			}
			if( t!= t_fr )
			{
				if( t > t_fr )
				{
					normal_id= BACK_LEFT;
					b= chunk_right_->GetBlock( 0, y + ((x+1)&1), z );
					light[0]= chunk_->SunLightLevel( x, y, z );
					light[1]= chunk_->FireLightLevel( x, y, z );
				}
				else
				{
					normal_id= FORWARD_RIGHT;
					b= chunk_->GetBlock( x, y, z );
					light[0]= chunk_right_->SunLightLevel( 0, y + ((x+1)&1), z );
					light[1]= chunk_right_->FireLightLevel( 0, y + ((x+1)&1), z );
				}
				BUILD_QUADS_FORWARD_RIGHT
			}
			if( t!= t_br )
			{
				if( t > t_br )
				{
					normal_id= FORWARD_LEFT;
					b= chunk_right_->GetBlock( 0, y - (x&1), z ) ;
					light[0]= chunk_->SunLightLevel( x, y, z );
					light[1]= chunk_->FireLightLevel( x, y, z );
				}
				else
				{
					normal_id= BACK_RIGHT;
					b= chunk_->GetBlock( x, y, z );
					light[0]= chunk_right_->SunLightLevel( 0, y - (x&1), z ) ;
					light[1]= chunk_right_->FireLightLevel( 0, y - (x&1), z ) ;
				}
				BUILD_QUADS_BACK_RIGHT
			}
			if( t!= t_f )
			{
				if( t > t_f )
				{
					normal_id= BACK;
					b= chunk_->GetBlock( x, y + 1, z );
					light[0]= chunk_->SunLightLevel( x, y, z );
					light[1]= chunk_->FireLightLevel( x, y, z );
				}
				else
				{
					normal_id= FORWARD;
					b= chunk_->GetBlock( x, y, z );
					light[0]= chunk_->SunLightLevel( x, y + 1, z );
					light[1]= chunk_->FireLightLevel( x, y + 1, z );
				}
				BUILD_QUADS_FORWARD
			}
		}
	}
#endif
#if 1
	//front chunk border ( x E[ 0; H_CHUNK_WIDTH - 2 ] )
	y= H_CHUNK_WIDTH - 1;
	for( x= 0; x < H_CHUNK_WIDTH - 1; x++ )
	{
		t_up= chunk_->Transparency( x, H_CHUNK_WIDTH - 1, 0 );
		for( z= 0; z< H_CHUNK_HEIGHT - 2; z++ )
		{
			t= t_up;
			if( x&1  )
				t_fr= chunk_->Transparency( x + 1, H_CHUNK_WIDTH - 1, z );//forward right
			else if( chunk_front_ != nullptr )
				t_fr= chunk_front_->Transparency( x + 1, 0, z );//forward right
			else t_fr= t;

			t_br= chunk_->Transparency( x + 1, H_CHUNK_WIDTH - 1 - ( 1&x ), z );//back right
			t_up= chunk_->Transparency( x, H_CHUNK_WIDTH - 1, z + 1 );//up

			if( chunk_front_ != nullptr )
				t_f= chunk_front_->Transparency( x, 0, z );//forward
			else
				t_f= t;

			if( t!= t_up )
			{
				BUILD_QUADS_UP
			}
			if( t!= t_fr )
			{
				if( t > t_fr )
				{
					normal_id= BACK_LEFT;
					b= ( x&1) ? chunk_->GetBlock( x + 1, H_CHUNK_WIDTH - 1, z ) :
					   chunk_front_->GetBlock( x + 1, 0, z );
					light[0]= chunk_->SunLightLevel( x, y, z );
					light[1]= chunk_->FireLightLevel( x, y, z );
				}
				else
				{
					normal_id= FORWARD_RIGHT;
					b= chunk_->GetBlock( x, y, z );
					light[0]= ( x&1) ? chunk_->SunLightLevel( x + 1, H_CHUNK_WIDTH - 1, z ) :
							  chunk_front_->SunLightLevel( x + 1, 0, z );
					light[1]= ( x&1) ? chunk_->FireLightLevel( x + 1, H_CHUNK_WIDTH - 1, z ) :
							  chunk_front_->FireLightLevel( x + 1, 0, z );
				}
				BUILD_QUADS_FORWARD_RIGHT
			}
			if( t!= t_br )
			{
				if( t > t_br )
				{
					normal_id= FORWARD_LEFT;
					b= chunk_->GetBlock( x + 1, y - (x&1), z );
					light[0]= chunk_->SunLightLevel( x, y, z );
					light[1]= chunk_->FireLightLevel( x, y, z );
				}
				else
				{
					normal_id= BACK_RIGHT;
					b= chunk_->GetBlock( x, y, z );
					light[0]= chunk_->SunLightLevel( x + 1, y - (x&1), z );
					light[1]= chunk_->FireLightLevel( x + 1, y - (x&1), z );
				}
				BUILD_QUADS_BACK_RIGHT
			}
			if( t!= t_f )
			{
				if( t > t_f )
				{
					normal_id= BACK;
					b= chunk_front_->GetBlock( x, 0, z );
					light[0]= chunk_->SunLightLevel( x, y, z );
					light[1]= chunk_->FireLightLevel( x, y, z );
				}
				else
				{
					normal_id= FORWARD;
					b= chunk_->GetBlock( x, y, z );
					light[0]= chunk_front_->SunLightLevel( x, 0, z );
					light[1]= chunk_front_->FireLightLevel( x, 0, z );
				}
				BUILD_QUADS_FORWARD
			}
		}
	}
#endif
#if 1
	//right up chunk corner
	x= y= H_CHUNK_WIDTH - 1;
	t_up= chunk_->Transparency( H_CHUNK_WIDTH - 1, H_CHUNK_WIDTH - 1, 0 );
	for( z= 0; z< H_CHUNK_HEIGHT - 2; z++ )
	{
		t= t_up;
		if( chunk_front_ != nullptr )
			t_f= chunk_front_->Transparency( H_CHUNK_WIDTH - 1, 0, z );
		else
			t_f= t;

		if( chunk_right_ != nullptr )
		{
			t_fr= chunk_right_->Transparency( 0, H_CHUNK_WIDTH - 1, z );
			t_br= chunk_right_->Transparency( 0, H_CHUNK_WIDTH - 2, z );
		}
		else
			t_fr= t_br= t;
		t_up= chunk_->Transparency( H_CHUNK_WIDTH - 1, H_CHUNK_WIDTH - 1, z + 1 );//up

		if( t!= t_up )
		{
			BUILD_QUADS_UP
		}
		if( t!= t_fr )
		{
			if( t > t_fr )
			{
				normal_id= BACK_LEFT;
				b= chunk_right_->GetBlock( 0, H_CHUNK_WIDTH  - 1, z );
				light[0]= chunk_->SunLightLevel( x, y, z );
				light[1]= chunk_->FireLightLevel( x, y, z );
			}
			else
			{
				normal_id= FORWARD_RIGHT;
				b= chunk_->GetBlock( x, y, z );
				light[0]= chunk_right_->SunLightLevel( 0, H_CHUNK_WIDTH  - 1, z );
				light[1]= chunk_right_->FireLightLevel( 0, H_CHUNK_WIDTH  - 1, z );
			}
			BUILD_QUADS_FORWARD_RIGHT
		}
		if( t!= t_br )
		{
			if( t > t_br )
			{
				normal_id= FORWARD_LEFT;
				b= chunk_right_->GetBlock( 0, H_CHUNK_WIDTH  - 2, z );
				light[0]= chunk_->SunLightLevel( x, y, z );
				light[1]= chunk_->FireLightLevel( x, y, z );
			}
			else
			{
				normal_id= BACK_RIGHT;
				b= chunk_->GetBlock( x, y, z );
				light[0]= chunk_right_->SunLightLevel( 0, H_CHUNK_WIDTH  - 2, z );
				light[1]= chunk_right_->FireLightLevel( 0, H_CHUNK_WIDTH  - 2, z );
			}
			BUILD_QUADS_BACK_RIGHT
		}
		if( t!= t_f )
		{
			if( t > t_f )
			{
				normal_id= BACK;
				b= chunk_front_->GetBlock( x, 0, z );
				light[0]= chunk_->SunLightLevel( x, y, z );
				light[1]= chunk_->FireLightLevel( x, y, z );
			}
			else
			{
				normal_id= FORWARD;
				b= chunk_->GetBlock( x, y, z );
				light[0]= chunk_front_->SunLightLevel( x, 0, z );
				light[1]= chunk_front_->FireLightLevel( x, 0, z );
			}
			BUILD_QUADS_FORWARD
		}
	}
#endif
#if 1
	//right down chunk corner
	x= H_CHUNK_WIDTH - 1, y=0;
	t_up= chunk_->Transparency( H_CHUNK_WIDTH - 1, 0, 0 );
	for( z= 0; z< H_CHUNK_HEIGHT - 2; z++ )
	{
		t= t_up;
		t_f= chunk_->Transparency( H_CHUNK_WIDTH - 1, 1, z );

		if( chunk_right_ != nullptr )
			t_fr= chunk_right_->Transparency( 0, 0, z );
		else
			t_fr= t;

		if( chunk_back_right_ !=nullptr )
			t_br= chunk_back_right_->Transparency( 0, H_CHUNK_WIDTH - 1, z );
		else
			t_br= t;
		t_up= chunk_->Transparency( H_CHUNK_WIDTH - 1, 0, z + 1 );//up

		if( t!= t_up )
		{
			BUILD_QUADS_UP
		}
		if( t!= t_fr )
		{
			if( t > t_fr )
			{
				normal_id= BACK_LEFT;
				b= chunk_right_->GetBlock( 0, 0, z );
				light[0]= chunk_->SunLightLevel( x, y, z );
				light[1]= chunk_->FireLightLevel( x, y, z );
			}
			else
			{
				normal_id= FORWARD_RIGHT;
				b= chunk_->GetBlock( x, y, z );
				light[0]= chunk_right_->SunLightLevel( 0, 0, z );
				light[1]= chunk_right_->FireLightLevel( 0, 0, z );
			}
			BUILD_QUADS_FORWARD_RIGHT
		}
		if( t!= t_br )
		{
			if( t > t_br )
			{
				normal_id= FORWARD_LEFT;
				b= chunk_back_right_->GetBlock( 0, H_CHUNK_WIDTH  - 1, z );
				light[0]= chunk_->SunLightLevel( x, y, z );
				light[1]= chunk_->FireLightLevel( x, y, z );
			}
			else
			{
				normal_id= BACK_RIGHT;
				b= chunk_->GetBlock( x, y, z );
				light[0]= chunk_back_right_->SunLightLevel( 0, H_CHUNK_WIDTH  - 1, z );
				light[1]= chunk_back_right_->FireLightLevel( 0, H_CHUNK_WIDTH  - 1, z );
			}
			BUILD_QUADS_BACK_RIGHT
		}
		if( t!= t_f )
		{
			if( t > t_f )
			{
				normal_id= BACK;
				b= chunk_->GetBlock( x, y + 1, z );
				light[0]= chunk_->SunLightLevel( x, y, z );
				light[1]= chunk_->FireLightLevel( x, y, z );
			}
			else
			{
				normal_id= FORWARD;
				b= chunk_->GetBlock( x, y, z );
				light[0]= chunk_->SunLightLevel( x, y + 1, z );
				light[1]= chunk_->FireLightLevel( x, y + 1, z );
			}
			BUILD_QUADS_FORWARD
		}
	}
#endif

}
