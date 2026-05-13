"""Entry point for python -m lvfont_tool."""

import sys


def main():
    # CLI mode: if any argument besides GUI flags
    if len(sys.argv) > 1 and sys.argv[1] in ("generate", "check", "--help", "-h"):
        from .cli import main as cli_main
        sys.exit(cli_main())
    else:
        from .app import main as gui_main
        gui_main()


main()
