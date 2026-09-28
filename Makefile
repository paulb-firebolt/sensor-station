.PHONY: docs open-docs clean-docs site site-dev

## Build API docs (Doxygen → doxygen/html/)
docs:
	./scripts/build-docs.sh

## Open API docs in browser
open-docs:
	./scripts/open-docs.sh

## Remove generated API docs
clean-docs:
	rm -rf doxygen/ public/doxygen/

## Build the full docs site (Doxygen + Blume → dist/)
site:
	npm run build:all

## Serve the docs site with hot reload (Doxygen served at /doxygen/)
site-dev:
	npm run api && npm run dev
