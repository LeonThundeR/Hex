#include <stdio.h>

#include <QApplication>

#include "world.hpp"
#include "thread.hpp"
#include "main_loop.hpp"
#include "console.hpp"

int main( int argc, char* argv[] )
{
    QApplication hex(argc, argv);
	h_Console::Message( "hex not first version" );
	h_MainLoop::Start();

    return hex.exec();
}
