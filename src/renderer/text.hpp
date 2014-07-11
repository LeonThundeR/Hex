#ifndef TEXT_H
#define TEXT_H

#include "glsl_program.h"
#include "polygon_buffer.h"
#include "texture.h"

class Texture;

#pragma pack( push, 1 )
struct r_TextVertex
{
	float pos[2];
	unsigned short tex_coord[2];
	unsigned char color[4];
};
#pragma pack (pop)


#define H_MAX_TEXT_BUFFER_SIZE 8192

//#define LETTER_WIDTH 8
//#define LETTER_HEIGHT 18
//#define FONT_BITMAP_WIDTH 768
//#define FONT_BITMAP_HEIGHT 18
#define LETTERS_IN_TEXTURE 96

class r_Text
{
	public:

	r_Text();
	~r_Text(){}
	void AddText( float colomn, float row, unsigned int size, const unsigned char* color, const char* text );
	void AddMultiText( float colomn, float row, unsigned int size, const unsigned char* color, const char* text, ... );

    void DrawCross();

	void Draw();
    void SetViewport( unsigned int x, unsigned int y );

    inline unsigned int ColomnsInScreen()const;
    inline unsigned int RowsInScreen()const;

	private:

	r_GLSLProgram text_shader;
	r_TextVertex* vertices;
    r_PolygonBuffer text_vbo;
	unsigned int vertex_buffer_size;
	unsigned int vertex_buffer_pos;
	float screen_x, screen_y;

    unsigned char* text_texture_data;
    unsigned int letter_width, letter_height;

	//GLuint font_texture_id;
	r_Texture font_texture;
    bool draw_crosshair;

public:
    static const unsigned char default_color[4];

    friend class Texture;

private:
    static r_Text* default_text;
public:
    static r_Text* DefaultText();

};

inline r_Text* r_Text::DefaultText()
{
    return default_text;
}

inline void r_Text::SetViewport( unsigned int x, unsigned int y )
{
    screen_x= float(x);
    screen_y= float(y);
}

 inline unsigned int r_Text::ColomnsInScreen()const
 {
 	return screen_x / letter_width;
 }
inline unsigned int r_Text::RowsInScreen()const
{
	return screen_y / letter_height;
}

#endif//TEXT_H
