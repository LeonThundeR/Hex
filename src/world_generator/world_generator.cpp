#include <cmath>
#include <vector>

#include <QImage>

#include "../math_lib/assert.hpp"
#include "../math_lib/m_math.h"
#include "../math_lib/rand.h"

#include "world_generator.hpp"


void PoissonDiskPoints(
	const unsigned int* size,
	unsigned char* out_data,
	unsigned int min_distanse_div_sqrt2)
{
	m_Rand randomizer;

	const float c_pi= 3.1415926535f;
	const int neighbor_k= 20;
	const float min_dst= float(min_distanse_div_sqrt2) * std::sqrt(2.0f) + 0.01f;
	const float min_dst2= min_dst * min_dst;

	unsigned int grid_size[2];
	for( unsigned int i= 0; i< 2; i++ )
	{
		grid_size[i]= size[i] / min_distanse_div_sqrt2;
		if( grid_size[i] * min_distanse_div_sqrt2 < size[i] ) grid_size[i]++;
	}

	unsigned int max_point_count= grid_size[0] * grid_size[1];
	std::vector<int> grid( max_point_count * 2, -1 );

	std::vector<int*> processing_stack;
	processing_stack.reserve(max_point_count);

	int init_pos[2];
	init_pos[0]= randomizer.RandI( size[0] );
	init_pos[1]= randomizer.RandI( size[1] );

	int init_pos_grid_pos[2];
	init_pos_grid_pos[0]= init_pos[0] / min_distanse_div_sqrt2;
	init_pos_grid_pos[1]= init_pos[1] / min_distanse_div_sqrt2;

	processing_stack.push_back( &grid[ (init_pos_grid_pos[0] + init_pos_grid_pos[1] * grid_size[0]) * 2 ] );
	processing_stack[0][0]= init_pos[0];
	processing_stack[0][1]= init_pos[1];

	while( !processing_stack.empty() )
	{
		int* current_point= processing_stack.back();
		processing_stack.pop_back();

		for( int n= 0; n< neighbor_k; n++ )
		{
			int* cell;
			int pos[2];
			int grid_pos[2];

			float angle= randomizer.RandF( c_pi * 2.0f );
			float r= randomizer.RandF( min_dst, min_dst * 2.0f );

			pos[0]= current_point[0] + int(r * std::cos(angle));
			pos[1]= current_point[1] + int(r * std::sin(angle));
			if( pos[0] < 0 || pos[0] >= int(size[0]) ||
				pos[1] < 0 || pos[1] >= int(size[1]) )
				continue;
			grid_pos[0]= pos[0] / min_distanse_div_sqrt2;
			grid_pos[1]= pos[1] / min_distanse_div_sqrt2;

			for( int y= std::max( grid_pos[1] - 4, 0 ); y< std::min( grid_pos[1] + 4, int(grid_size[1]) ); y++ )
			for( int x= std::max( grid_pos[0] - 4, 0 ); x< std::min( grid_pos[0] + 4, int(grid_size[1]) ); x++ )
			{
				int* cell= &grid[ (x + y * grid_size[0]) * 2 ];
				if( cell[0] != -1 )
				{
					int d_dst[2];
					d_dst[0]= pos[0] - cell[0];
					d_dst[1]= pos[1] - cell[1];

					if( float( d_dst[0] * d_dst[0] + d_dst[1] * d_dst[1] ) < min_dst2 )
						goto xy_loop_break;
				}
			} // search points in nearby grid cells

			cell= &grid[ (grid_pos[0] + grid_pos[1] * grid_size[0]) * 2 ];
			H_ASSERT( cell[0] == -1 );
			cell[0]= pos[0];
			cell[1]= pos[1];
			H_ASSERT( processing_stack_pos < max_point_count );
			processing_stack.push_back( cell );

			xy_loop_break:;
		} // try place points near
	} // for points in stack

	float intencity_multipler= 255.0f / float(min_distanse_div_sqrt2 * 3);

	// Draw Voronoy diagramm.
	unsigned char* d= out_data;
	for( int y= 0; y< int(size[1]); y++ )
	{
		int grid_pos[2];
		grid_pos[1]= y / min_distanse_div_sqrt2;
		for( int x= 0; x< int(size[0]); x++, d+= 4 )
		{
			grid_pos[0]= x / min_distanse_div_sqrt2;

			int nearest_point_dst2[2]= { 0xfffffff, 0xfffffff };

			for( int v= std::max( grid_pos[1] - 3, 0 ); v< std::min( grid_pos[1] + 4, int(grid_size[1]) ); v++ )
			for( int u= std::max( grid_pos[0] - 3, 0 ); u < std::min( grid_pos[0] + 4, int(grid_size[0]) ); u++ )
			{
				int* cell= &grid[ (u + v * grid_size[0]) * 2 ];
				if( cell[0] != -1 )
				{
					int d_dst[2];
					d_dst[0]= cell[0] - x;
					d_dst[1]= cell[1] - y;

					int dst2= d_dst[0] * d_dst[0] + d_dst[1] * d_dst[1];
					if( dst2 < nearest_point_dst2[0] )
					{
						nearest_point_dst2[1]= nearest_point_dst2[0];
						nearest_point_dst2[0]= dst2;
					}
					else if( dst2 < nearest_point_dst2[1] )
						nearest_point_dst2[1]= dst2;
				}
			} // for grid uv

			// borders - like points
			{
				int dst2;
				dst2= x * x;
				if( dst2 < nearest_point_dst2[0] ) nearest_point_dst2[0]= dst2;
				dst2= y * y;
				if( dst2 < nearest_point_dst2[0] ) nearest_point_dst2[0]= dst2;
				dst2= int(size[0]) - x; dst2= dst2 * dst2;
				if( dst2 < nearest_point_dst2[0] ) nearest_point_dst2[0]= dst2;
				dst2= int(size[1]) - y; dst2= dst2 * dst2;
				if( dst2 < nearest_point_dst2[0] ) nearest_point_dst2[0]= dst2;
			}

			d[0]= (unsigned char)( std::sqrt(float(nearest_point_dst2[0])) * intencity_multipler );
			d[1]= d[2]= d[3]= 0;

			//d[1]= (unsigned char)( ( std::sqrt(float(nearest_point_dst2[1])) - std::sqrt(float(nearest_point_dst2[0])) ) * intencity_multipler );
			//d[2]= (unsigned char)( std::sqrt(float(nearest_point_dst2[1])) * intencity_multipler );

		} // fot x
	} // for y
}

g_WorldGenerator::g_WorldGenerator(const g_WorldGenerationParameters& parameters)
	: parameters_(parameters)
{
}

void g_WorldGenerator::Generate()
{
	unsigned int size[2]= { 1024, 1024 };
	std::vector<unsigned char> data( size[0] * size[1] * 4 );
	PoissonDiskPoints( size, data.data(), 73 );

	QImage img( size[0], size[1], QImage::Format_RGBX8888 );
	img.fill(Qt::black);

	memcpy( img.bits(), data.data(), size[0] * size[1] * 4 );
	img.save( "heightmap.png" );
}

void g_WorldGenerator::DumpDebugResult()
{
}
