#pragma once

class m_Rand
{
public:
	m_Rand();
	m_Rand( unsigned int s );

	void SetSeed( unsigned int s );
	unsigned int Rand();
	float RandIdentity();//returns random value in range [0.0f;1.0f]

	// Returns values in range [0; max)
	unsigned int RandI( unsigned int max );
	float RandF( float max );

	// Returns values in range [min; max)
	unsigned int RandI( int min, int max );
	float RandF( float min, float max );

	unsigned int operator()();

	const static constexpr unsigned int max_rand= 0x7FFF;
private:
	unsigned int x;
};

inline void m_Rand::SetSeed( unsigned int s )
{
	x= s;
}
inline m_Rand::m_Rand( unsigned int s )
{
	x= s;
}
inline m_Rand::m_Rand()
{
	x= 0;
}

inline unsigned int m_Rand::Rand()
{
	x= ( ( 22695477 * x + 1 ) & 0x7FFFFFFF );
	return x>>16;
}

inline unsigned int m_Rand::operator()()
{
	return Rand();
}

inline float m_Rand::RandIdentity()
{
	return float( Rand() ) / float( max_rand );
}

inline unsigned int m_Rand::RandI( unsigned int max )
{
	return Rand() * max / max_rand;
}

inline float m_Rand::RandF( float max )
{
	return float(Rand()) * max / float(max_rand);
}

inline unsigned int m_Rand::RandI( int min, int max )
{
	return int(Rand()) * (max - min) / int(max_rand) + min;
}

inline float m_Rand::RandF( float min, float max )
{
	return float(Rand()) * (max - min) / float(max_rand) + min;
}
