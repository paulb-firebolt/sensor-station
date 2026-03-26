.PHONY: docs open-docs clean-docs

## Build API docs (Doxygen → doxygen/html/)
docs:
	./scripts/build-docs.sh

## Open API docs in browser
open-docs:
	./scripts/open-docs.sh

## Remove generated API docs
clean-docs:
	rm -rf doxygen/
