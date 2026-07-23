"""Entry point: python -m playground.studio.server [--host HOST] [--port PORT]."""

from __future__ import annotations

import argparse
import webbrowser


def main() -> None:
    parser = argparse.ArgumentParser(
        description="AGenUI Studio - Local A2UI Generation Server"
    )
    parser.add_argument("--host", default=None, help="Bind host (default: from config or 0.0.0.0)")
    parser.add_argument("--port", type=int, default=None, help="Bind port (default: from config or 8765)")
    parser.add_argument("--no-browser", action="store_true", help="Do not auto-open browser")
    args = parser.parse_args()

    from .config import get_lan_ip, load_config

    cfg = load_config()
    host = args.host or cfg.host
    port = args.port or cfg.port
    lan_ip = get_lan_ip()

    print("AGenUI Studio starting...")
    print(f"  Local:   http://127.0.0.1:{port}")
    print(f"  Network: http://{lan_ip}:{port}")
    print()

    if not args.no_browser:
        webbrowser.open(f"http://127.0.0.1:{port}")

    import uvicorn

    uvicorn.run(
        "playground.studio.server.server:app",
        host=host,
        port=port,
        log_level="info",
    )


if __name__ == "__main__":
    main()
