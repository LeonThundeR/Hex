#pragma once
#include <string>
#include <vector>

struct g_WorldGenerationParameters
{
	std::string world_dir;

	// Size in units.
	unsigned int size[2];
	unsigned int seed;
};

class g_WorldGenerator
{
public:
	explicit g_WorldGenerator(const g_WorldGenerationParameters& parameters);

	void Generate();
	void DumpDebugResult();

	unsigned char GetGroundLevel( int x, int y ) const;
	unsigned char GetSeaLevel() const;

private:

	enum class Biome
	{
		Sea= 0,
		ContinentalShelf,
		SeaBeach,
		Plains,
		Mountains,
		LastBiome,
	};

	// For debugging.
	static const unsigned char c_biomes_colors_[ size_t(Biome::LastBiome) * 4 ];

	void BuildPrimaryHeightmap();
	void BuildSecondaryHeightmap();
	void BuildBiomesMap();

	// returns interpolated heightmap value * 256
	unsigned int HeightmapValueInterpolated( int x, int y ) const;

private:
	g_WorldGenerationParameters parameters_;

	std::vector<unsigned char> primary_heightmap_;
	const unsigned char primary_heightmap_sea_level_= 44;

	std::vector<unsigned char> secondary_heightmap_;
	const unsigned char secondary_heightmap_sea_level_= 48;
	const unsigned char secondary_heightmap_sea_bottom_level_= 20;
	const unsigned char secondary_heightmap_mountain_top_level_= 110;

	std::vector<Biome> biomes_map_;
};
