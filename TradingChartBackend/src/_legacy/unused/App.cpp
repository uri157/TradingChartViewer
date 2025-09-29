// #include "App.h"
// #include <SFML/Graphics/RenderWindow.hpp>
// #include <SFML/Window/Event.hpp>
// #include <iostream>
// #include "ChartsScene.h"

// App::App():w(sf::VideoMode::getDesktopMode(),"The Trading Project", sf::Style::Fullscreen), crsor(w) {
// 	actualScene=new ChartsScene(w,crsor);
// 	w.setFramerateLimit(60);
// }

// void App::runApp(){
// 	w.setMouseCursorVisible(false); 
	
// 	while(w.isOpen()) {
// 		sf::Event e;
// 		while(w.pollEvent(e)) {
// 			if(e.type == sf::Event::Closed)
// 				w.close();	
			
// 		}

// 		w.clear(sf::Color(20, 20, 20)); // RGB: (33, 37, 43)
		
// 		actualScene->actualize(e);
// 		actualScene->draw();
// 		w.display();
// 	}
// }

// App::~App(){
// 	delete actualScene;
// }
