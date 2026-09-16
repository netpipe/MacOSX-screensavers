NAME   = NeonClock
BUNDLE = $(NAME).saver
ARCHS  = -arch arm64 -arch x86_64

all: $(BUNDLE)

$(BUNDLE): $(NAME)View.m Info.plist
	rm -rf $(BUNDLE)
	mkdir -p $(BUNDLE)/Contents/MacOS
	cp Info.plist $(BUNDLE)/Contents/
	clang -bundle $(ARCHS) -O2 -fobjc-arc -Wall -Wno-deprecated-declarations \
	    -mmacosx-version-min=11.0 \
	    $(NAME)View.m \
	    -framework ScreenSaver -framework OpenGL -framework Cocoa \
	    -o $(BUNDLE)/Contents/MacOS/$(NAME)

install: $(BUNDLE)
	mkdir -p "$(HOME)/Library/Screen Savers"
	rm -rf "$(HOME)/Library/Screen Savers/$(BUNDLE)"
	cp -R $(BUNDLE) "$(HOME)/Library/Screen Savers/"

clean:
	rm -rf $(BUNDLE)

.PHONY: all install clean