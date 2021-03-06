#include "world_common.glsl"

uniform vec3 sun_vector;

uniform sampler2D tex;
uniform float time;
uniform vec3 sun_light_color;
uniform vec3 fire_light_color;
uniform vec3 ambient_light_color;

in vec3 f_normal;
in vec2 f_tex_coord;
in vec2 f_light;

out vec4 color;

void main()
{
	vec3 l= CombineLight( f_light.x * sun_light_color, f_light.y * fire_light_color, ambient_light_color );

	vec2 tc= f_tex_coord + sin( f_tex_coord.yx * 8.0 + vec2( time, time ) ) * 0.06125;
	vec4 c= HexagonFetch( tex, tc );

	color= vec4( l * c.xyz, c.a );
}
