#include "../math_lib/assert.hpp"
#include "../math_lib/math.hpp"
#include "wvb.hpp"

#define H_BUFFER_OBJECT_NOT_CREATED 0xFFFFFFFF

r_WorldVBOCluster::r_WorldVBOCluster()
	: buffer_reallocated_( true )
{
}

/*
----------------r_WorldVBOClusterSegment----------
*/

r_WorldVBOClusterSegment::r_WorldVBOClusterSegment()
	: first_vertex_index(0)
	, vertex_count(0)
	, capacity(0)
	, updated(false)
{
}

/*
----------------r_WorldVBOClusterGPU-----------
*/

r_WorldVBOClusterGPU::r_WorldVBOClusterGPU(
	const r_WorldVBOClusterPtr& cpu_cluster,
	const r_VertexFormat& vertex_format,
	GLuint index_buffer )
	: cluster_( cpu_cluster )
	, vertex_size_( vertex_format.vertex_size )
{
	// TODO - add thread check

	glGenVertexArrays( 1, &VAO_ );
	glBindVertexArray( VAO_ );

	glGenBuffers( 1, &VBO_ );
	glBindBuffer( GL_ARRAY_BUFFER, VBO_ );

	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, index_buffer );

	unsigned int i= 0;
	for( const r_VertexFormat::Attribute& attribute : vertex_format.attributes )
	{
		glEnableVertexAttribArray(i);

		if( attribute.type == r_VertexFormat::Attribute::TypeInShader::Integer )
			glVertexAttribIPointer(
				i,
				attribute.components, attribute.input_type,
				vertex_format.vertex_size, (void*) attribute.offset );
		else
			glVertexAttribPointer(
				i,
				attribute.components, attribute.input_type, attribute.normalized,
				vertex_format.vertex_size, (void*) attribute.offset );

		i++;
	}
}

r_WorldVBOClusterGPU::~r_WorldVBOClusterGPU()
{
	// TODO - add thread check

	glDeleteBuffers( 1, &VBO_ );
	glDeleteVertexArrays( 1, &VAO_ );
}

void r_WorldVBOClusterGPU::SynchroniseSegmentsInfo(
	unsigned int cluster_size_x, unsigned int cluster_size_y )
{
	r_WorldVBOClusterPtr cluster= cluster_.lock();
	if( !cluster )
	{
		for( unsigned int i= 0; i < cluster_size_x * cluster_size_y; i++ )
			segments_[i].updated= false;
		buffer_reallocated_= false;
	}
	else
	{
		for( unsigned int i= 0; i < cluster_size_x * cluster_size_y; i++ )
		{
			segments_[i]= cluster->segments_[i];
			cluster->segments_[i].updated= false;
		}

		buffer_reallocated_= cluster->buffer_reallocated_;
		cluster->buffer_reallocated_= false;
	}
}

void r_WorldVBOClusterGPU::UpdateVBO(
	unsigned int cluster_size_x, unsigned int cluster_size_y )
{
	r_WorldVBOClusterPtr cluster= cluster_.lock();
	if( !cluster ) return;

	glBindVertexArray( VAO_ );
	glBindBuffer( GL_ARRAY_BUFFER, VBO_ ); // TODO - Does this need?

	if( buffer_reallocated_ )
	{
		if( cluster->vertices_.size() > 0 )
		{
			glBufferData(
				GL_ARRAY_BUFFER,
				cluster->vertices_.size(), cluster->vertices_.data(),
				GL_STATIC_DRAW );
		}
	}
	else
	{
		for( unsigned int i= 0; i < cluster_size_x * cluster_size_y; i++ )
		{
			if( segments_[i].updated && segments_[i].vertex_count > 0 )
			{
				unsigned int offset= segments_[i].first_vertex_index * vertex_size_;

				glBufferSubData(
					GL_ARRAY_BUFFER,
					offset,
					segments_[i].vertex_count * vertex_size_,
					cluster->vertices_.data() + offset );
			}
		}
	}

	// Clear update flags.
	buffer_reallocated_= false;
	for( unsigned int i= 0; i < cluster_size_x * cluster_size_y; i++ )
		segments_[i].updated= false;
}

void r_WorldVBOClusterGPU::BindVBO()
{
	glBindVertexArray( VAO_ );
}

/*
---------------r_WVB-------------------
*/

r_WVB::r_WVB(
	unsigned int cluster_size_x, unsigned int cluster_size_y,
	unsigned int cluster_matrix_size_x, unsigned int cluster_matrix_size_y,
	std::vector<unsigned short> indeces,
	r_VertexFormat vertex_format )
	: cluster_size_{ cluster_size_x, cluster_size_y }
	, cluster_matrix_size_{ cluster_matrix_size_x, cluster_matrix_size_y }
	, cpu_cluster_matrix_( cluster_matrix_size_x * cluster_matrix_size_y )
	, cpu_cluster_matrix_coord_{ 0, 0 }
	, gpu_cluster_matrix_( cluster_matrix_size_x * cluster_matrix_size_y )
	, gpu_cluster_matrix_coord_{ 0, 0 }
	, index_buffer_( H_BUFFER_OBJECT_NOT_CREATED )
	, indeces_( std::move(indeces) )
	, vertex_format_( std::move(vertex_format) )
{
	H_ASSERT( cluster_size_x <= H_MAX_CHUNKS_IN_CLUSTER && cluster_size_y <= H_MAX_CHUNKS_IN_CLUSTER );

	for( unsigned int y= 0; y < cluster_matrix_size_[1]; y++ )
		for( unsigned int x= 0; x < cluster_matrix_size_[0]; x++ )
			cpu_cluster_matrix_[ x + y * cluster_matrix_size_[0] ]=
				std::make_shared<r_WorldVBOCluster>();
}

r_WVB::~r_WVB()
{
	if( index_buffer_ != H_BUFFER_OBJECT_NOT_CREATED )
	{
		glDeleteBuffers( 1, &index_buffer_ );
	}
}

GLuint r_WVB::GetIndexBuffer()
{
	if( index_buffer_ == H_BUFFER_OBJECT_NOT_CREATED )
	{
		// Clear VAO, if we can not hit it by binding of this index buffer.
		glBindVertexArray( 0 );

		glGenBuffers( 1, &index_buffer_ );
		glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, index_buffer_ );

		glBufferData(
			GL_ELEMENT_ARRAY_BUFFER,
			indeces_.size() * sizeof(unsigned short),
			indeces_.data(),
			GL_STATIC_DRAW );
	}

	return index_buffer_;
}

void r_WVB::MoveCPUMatrix( short longitude, short latitude )
{
	H_ASSERT( m_Math::ModNonNegativeRemainder( longitude, cluster_size_[0] ) == 0 );
	H_ASSERT( m_Math::ModNonNegativeRemainder( latitude , cluster_size_[1] ) == 0 );

	if( longitude == cpu_cluster_matrix_coord_[0] &&
		latitude  == cpu_cluster_matrix_coord_[1] )
		return;

	int dx= ( longitude - cpu_cluster_matrix_coord_[0] ) / (int)cluster_size_[0];
	int dy= ( latitude  - cpu_cluster_matrix_coord_[1] ) / (int)cluster_size_[1];

	std::vector<r_WorldVBOClusterPtr> new_matrix( cluster_matrix_size_[0] * cluster_matrix_size_[1] );

	for( int y= 0; y < (int)cluster_matrix_size_[1]; y++ )
	for( int x= 0; x < (int)cluster_matrix_size_[0]; x++ )
	{
		int old_x= x + dx;
		int old_y= y + dy;
		if( old_x >= 0 && old_x < (int)cluster_matrix_size_[0] &&
			old_y >= 0 && old_y < (int)cluster_matrix_size_[1] )
		{
			new_matrix[x + y * cluster_matrix_size_[0]]=
				std::move(
					cpu_cluster_matrix_[ old_x + old_y * cluster_matrix_size_[0] ] );
		}
		else
		{
			new_matrix[x + y * cluster_matrix_size_[0]]=
				std::make_shared<r_WorldVBOCluster>();
		}
	}

	cpu_cluster_matrix_= std::move(new_matrix);
	cpu_cluster_matrix_coord_[0]= longitude;
	cpu_cluster_matrix_coord_[1]= latitude ;
}

void r_WVB::UpdateGPUMatrix( short longitude, short latitude )
{
	H_ASSERT( m_Math::ModNonNegativeRemainder( longitude, cluster_size_[0] ) == 0 );
	H_ASSERT( m_Math::ModNonNegativeRemainder( latitude , cluster_size_[1] ) == 0 );

	GLuint index_buffer= GetIndexBuffer();

	if( !gpu_cluster_matrix_[0] )
	// Not initialized yet - perform initialization.
	{
		for( unsigned int i= 0; i < gpu_cluster_matrix_.size(); i++ )
			gpu_cluster_matrix_[i].reset(
				new r_WorldVBOClusterGPU(
					cpu_cluster_matrix_[i], vertex_format_, index_buffer ) );

		gpu_cluster_matrix_coord_[0]= longitude;
		gpu_cluster_matrix_coord_[1]= latitude ;
		return;
	}

	int dx= ( longitude - gpu_cluster_matrix_coord_[0] ) / (int)cluster_size_[0];
	int dy= ( latitude  - gpu_cluster_matrix_coord_[1] ) / (int)cluster_size_[1];
	if( dx == 0 && dy == 0 ) return;

	std::vector<r_WorldVBOClusterGPUPtr> new_matrix( cluster_matrix_size_[0] * cluster_matrix_size_[1] );

	for( int y= 0; y < (int)cluster_matrix_size_[1]; y++ )
	for( int x= 0; x < (int)cluster_matrix_size_[0]; x++ )
	{
		int old_x= x + dx;
		int old_y= y + dy;
		if( old_x >= 0 && old_x < (int)cluster_matrix_size_[0] &&
			old_y >= 0 && old_y < (int)cluster_matrix_size_[1] )
			new_matrix[ x + y * cluster_matrix_size_[0] ]=
				std::move( gpu_cluster_matrix_[ old_x + old_y * cluster_matrix_size_[0] ] );
		else
			new_matrix[ x + y * cluster_matrix_size_[0] ].reset(
				new r_WorldVBOClusterGPU(
					cpu_cluster_matrix_[ x + y * cluster_matrix_size_[0] ],
					vertex_format_,
					index_buffer ) );
	}

	gpu_cluster_matrix_= std::move(new_matrix);
	gpu_cluster_matrix_coord_[0]= longitude;
	gpu_cluster_matrix_coord_[1]= latitude ;
}

r_WorldVBOCluster& r_WVB::GetCluster( int longitude, int latitude )
{
	int x= (longitude - cpu_cluster_matrix_coord_[0]) / cluster_size_[0];
	int y= (latitude  - cpu_cluster_matrix_coord_[1]) / cluster_size_[1];

	H_ASSERT( x >= 0 && x < (int)cluster_matrix_size_[0] );
	H_ASSERT( y >= 0 && y < (int)cluster_matrix_size_[1] );

	return *( cpu_cluster_matrix_[ x + y * cluster_matrix_size_[0] ] );
}

r_WorldVBOClusterSegment& r_WVB::GetClusterSegment( int longitude, int latitude )
{
	int d_lon= (longitude - cpu_cluster_matrix_coord_[0]);
	int d_lat= (latitude  - cpu_cluster_matrix_coord_[1]);

	int cluster_x= d_lon / cluster_size_[0];
	int cluster_y= d_lat / cluster_size_[1];

	H_ASSERT( cluster_x >= 0 && cluster_x < (int)cluster_matrix_size_[0] );
	H_ASSERT( cluster_y >= 0 && cluster_y < (int)cluster_matrix_size_[1] );

	int segment_x= d_lon - cluster_x * cluster_size_[0];
	int segment_y= d_lat - cluster_y * cluster_size_[1];

	r_WorldVBOClusterPtr& cluster=
		cpu_cluster_matrix_[ cluster_x + cluster_y * cluster_matrix_size_[0] ];

	return cluster->segments_[ segment_x + segment_y * cluster_size_[0] ];
}
