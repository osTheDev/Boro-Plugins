# Builds every plugin into dist/SEPLUGINS/.
PLUGINS = bright status hold snap

all:
	@for p in $(PLUGINS); do $(MAKE) -C $$p || exit 1; done
	mkdir -p dist/SEPLUGINS
	for p in $(PLUGINS); do cp $$p/boro_$$p.prx dist/SEPLUGINS/; done
	cp boro.ini dist/SEPLUGINS/

clean:
	@for p in $(PLUGINS); do $(MAKE) -C $$p clean; done
	rm -f common/*.o
	rm -rf dist
