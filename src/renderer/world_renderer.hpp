#pragma once
#include <mutex>
#include <ctime>

#include "../hex.hpp"
#include "../fwd.hpp"
#include "../ticks_counter.hpp"
#include "i_world_renderer.hpp"
#include "chunk_data_cache.hpp"

#include "framebuffer_texture.hpp"
#include "framebuffer.hpp"
#include "polygon_buffer.hpp"
#include "glsl_program.hpp"
#include "text.hpp"
#include "texture_manager.hpp"
#include "weather_effects_particle_manager.hpp"

#include "wvb.hpp"

#include "matrix.hpp"
#include "../math_lib/collection.hpp"

#pragma pack( push, 1 )

struct r_WorldVertex
{
	short coord[3];
	short tex_coord[3];
	unsigned char light[2];
	unsigned char normal_id;
	char reserved[1];
};//16b struct

struct r_WaterVertex
{
	short coord[3];
	unsigned char light[2];
};//8b struct

#pragma pack( pop )


class r_ChunkInfo
{
public:
	r_ChunkInfo();

	void GetWaterHexCount();
	void BuildWaterSurfaceMesh();

	void GetQuadCount();
	void BuildChunkMesh();

	// Pointer to external storage for vertices.
	r_WorldVertex* vertex_data_= nullptr;
	unsigned int vertex_count_= 0;
	// ChunkInfo is always updated after creation.
	bool updated_= true;

	r_WaterVertex* water_vertex_data_= nullptr;
	unsigned int water_vertex_count_= 0;
	bool water_updated_= true;

	// Flags, setted by world.
	// Chunk can really updates later, after this flags setted.
	bool update_requested_= false;
	bool water_update_requested_= false;

	//geomentry up and down range borders. Used only for generation of center chunk blocks( not for border blocks )
	int max_geometry_height_, min_geometry_height_;

	const h_Chunk* chunk_;
	const h_Chunk* chunk_front_, *chunk_right_, *chunk_back_right_, *chunk_back_;
};

typedef std::unique_ptr<r_ChunkInfo> r_ChunkInfoPtr;

class r_WorldRenderer final : public r_IWorldRenderer
{
public:
	r_WorldRenderer( const h_SettingsPtr& settings, const h_WorldConstPtr& world );
	virtual ~r_WorldRenderer() override;

public: // r_IWorldRenderer
	virtual void Update() override;
	virtual void UpdateChunk( unsigned short, unsigned short ) override;
	virtual void UpdateChunkWater( unsigned short, unsigned short ) override;
	virtual void UpdateWorldPosition( int longitude, int latitude ) override;

public:
	void Draw();

	void Init();
	void InitGL();

	void SetCamPos( const m_Vec3& p );
	void SetCamAng( const m_Vec3& a );
	void SetBuildPos( const m_Vec3& p, h_Direction direction );
	void SetViewportSize( unsigned int viewport_width, unsigned int viewport_height );

private:
	r_WorldRenderer& operator=( const r_WorldRenderer& other ) = delete;

	void LoadShaders();
	void LoadTextures();
	void InitFrameBuffers();
	void InitVertexBuffers();

	// Recalc pointers to parent h_Chunk and his neighbors.
	void UpdateChunkMatrixPointers();
	void MoveChunkMatrix( int longitude, int latitude );
	// Coordinates - in chunks matrix.
	bool NeedRebuildChunkInThisTick( unsigned int x, unsigned int y );

	void UpdateGPUData();

	void CalculateMatrices();
	void CalculateLight();

	void DrawBuildPrism();

	// helper. returns vertex count
	void CalculateChunksVisibility();
	unsigned int DrawClusterMatrix( r_WVB* wvb, unsigned int indeces_per_vertex_num, unsigned int indeces_per_vertex_den );
	void DrawWorld();
	void DrawWater();

	void DrawSky();
	void DrawSun();
	void DrawConsole();

private:
	const h_SettingsPtr settings_;
	const h_WorldConstPtr world_;

	// Counters
	h_TicksCounter frames_counter_;
	h_TicksCounter chunks_updates_counter_;
	h_TicksCounter chunks_water_updates_counter_;
	h_TicksCounter updates_counter_;
	unsigned int world_quads_in_frame_;
	unsigned int water_hexagons_in_frame_;
	unsigned int chunks_visible_;

	// Shaders
	r_GLSLProgram world_shader_, build_prism_shader_, water_shader_, skybox_shader_, sun_shader_, console_bg_shader_, supersampling_final_shader_;

	//VBO
	r_PolygonBuffer build_prism_vbo_;
	r_PolygonBuffer skybox_vbo_;

	struct
	{
		m_Vec3 current_sun_light;
		m_Vec3 current_fire_light;
		m_Vec3 sun_direction;
	} lighting_data_;

	//framebuffers
	unsigned viewport_width_, viewport_height_;
	r_Framebuffer supersampling_buffer_;
	bool use_supersampling_;
	unsigned int pixel_size_;

	//textures
	r_TextureManager texture_manager_;
	r_FramebufferTexture sun_texture_;
	r_FramebufferTexture water_texture_;
	r_FramebufferTexture console_bg_texture_;

	//matrices and vectors
	float fov_x_, fov_y_;
	m_Mat4 view_matrix_, block_scale_matrix_, block_final_matrix_, water_final_matrix_;
	m_Vec3 cam_ang_, cam_pos_;

	m_Vec3 build_pos_;
	h_Direction build_direction_;

	struct
	{
		std::vector< r_ChunkInfoPtr > chunk_matrix;
		// same as world size
		unsigned int matrix_size[2];
		// Longitude + latitude
		int matrix_position[2];
	} chunks_info_;

	// Read in GPU thread
	struct
	{
		std::vector<bool> chunks_visibility_matrix;
		// Longitude + latitude
		int matrix_position[2];
	} chunks_info_for_drawing_;

	std::unique_ptr<r_WVB> world_vertex_buffer_;
	std::unique_ptr<r_WVB> world_water_vertex_buffer_;
	std::mutex world_vertex_buffer_mutex_;

	//text out
	r_Text* text_manager_;

	r_WeatherEffectsParticleManager weather_effects_particle_manager_;

	time_t startup_time_;
};

inline void r_WorldRenderer::SetCamPos( const m_Vec3& p )
{
	cam_pos_= p;
}

inline void r_WorldRenderer::SetCamAng( const m_Vec3& a )
{
	cam_ang_= a;
}

inline void r_WorldRenderer::SetBuildPos( const m_Vec3& p, h_Direction direction )
{
	build_pos_= p;
	build_direction_= direction;
}

inline void r_WorldRenderer::SetViewportSize( unsigned int viewport_width, unsigned int viewport_height )
{
	viewport_width_= viewport_width;
	viewport_height_= viewport_height;
}
