from __future__ import annotations

import json
import os
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from dotenv import load_dotenv


APP_NAME = "ApexAutoTrader"
LIVE_ACK_PHRASE = "I_ACCEPT_AUTONOMOUS_TRADING_RISK"


def _parse_bool(value: str | bool | None, default: bool = False) -> bool:
    if value is None:
        return default
    if isinstance(value, bool):
        return value
    return value.strip().lower() in {"1", "true", "yes", "y", "on"}


def _parse_list(value: str | list[str] | None, default: list[str]) -> list[str]:
    if value is None:
        return list(default)
    if isinstance(value, list):
        return [str(item).strip().upper() for item in value if str(item).strip()]
    return [item.strip().upper() for item in value.split(",") if item.strip()]


def _read_json(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {}
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


@dataclass(slots=True)
class RiskConfig:
    max_trade_fraction: float = 0.05
    max_symbol_fraction: float = 0.25
    max_open_positions: int = 8
    stop_loss_pct: float = 0.06
    take_profit_pct: float = 0.10
    pause_drawdown_pct: float = 0.20
    min_cash_reserve_pct: float = 0.05
    max_daily_trades: int = 6
    min_order_notional: float = 1.0
    allow_fractional: bool = True
    allow_crypto: bool = True
    allow_short_selling: bool = False
    allow_options: bool = False
    allow_margin: bool = False

    @classmethod
    def from_dict(cls, raw: dict[str, Any] | None) -> "RiskConfig":
        raw = raw or {}
        risk = cls(
            max_trade_fraction=float(raw.get("max_trade_fraction", cls.max_trade_fraction)),
            max_symbol_fraction=float(raw.get("max_symbol_fraction", cls.max_symbol_fraction)),
            max_open_positions=int(raw.get("max_open_positions", cls.max_open_positions)),
            stop_loss_pct=float(raw.get("stop_loss_pct", cls.stop_loss_pct)),
            take_profit_pct=float(raw.get("take_profit_pct", cls.take_profit_pct)),
            pause_drawdown_pct=float(raw.get("pause_drawdown_pct", cls.pause_drawdown_pct)),
            min_cash_reserve_pct=float(raw.get("min_cash_reserve_pct", cls.min_cash_reserve_pct)),
            max_daily_trades=int(raw.get("max_daily_trades", cls.max_daily_trades)),
            min_order_notional=float(raw.get("min_order_notional", cls.min_order_notional)),
            allow_fractional=_parse_bool(raw.get("allow_fractional"), cls.allow_fractional),
            allow_crypto=_parse_bool(raw.get("allow_crypto"), cls.allow_crypto),
            allow_short_selling=False,
            allow_options=False,
            allow_margin=False,
        )
        risk.validate()
        return risk

    def validate(self) -> None:
        if not 0 < self.max_trade_fraction <= 0.10:
            raise ValueError("risk.max_trade_fraction must be > 0 and <= 0.10")
        if not 0 < self.max_symbol_fraction <= 0.50:
            raise ValueError("risk.max_symbol_fraction must be > 0 and <= 0.50")
        if self.pause_drawdown_pct > 0.20:
            raise ValueError("risk.pause_drawdown_pct must be <= 0.20")
        if self.max_open_positions < 1:
            raise ValueError("risk.max_open_positions must be >= 1")
        if self.max_daily_trades < 1:
            raise ValueError("risk.max_daily_trades must be >= 1")
        if self.stop_loss_pct <= 0 or self.take_profit_pct <= 0:
            raise ValueError("risk stop-loss and take-profit percentages must be positive")


@dataclass(slots=True)
class AppConfig:
    openai_api_key: str
    openai_model: str = "gpt-5.5"
    openai_reasoning_effort: str = "medium"
    openai_max_output_tokens: int = 4000

    mcp_url: str = "https://agent.robinhood.com/mcp/trading"
    mcp_transport: str = "stdio"
    mcp_command: str = "npx"
    mcp_args: list[str] = field(
        default_factory=lambda: ["-y", "mcp-remote@latest", "https://agent.robinhood.com/mcp/trading"]
    )
    mcp_timeout_seconds: int = 45

    trading_mode: str = "dry_run"
    autonomous_ack: str = ""
    loop_interval_minutes: int = 15
    loop_jitter_seconds: int = 120
    watchlist: list[str] = field(default_factory=lambda: ["SPY", "QQQ", "AAPL", "MSFT", "NVDA", "AMZN", "META"])
    crypto_watchlist: list[str] = field(default_factory=lambda: ["BTC-USD", "ETH-USD"])
    news_queries: list[str] = field(default_factory=lambda: ["market moving news", "federal reserve rates", "stock market"])
    data_dir: Path = field(default_factory=lambda: Path("runtime"))
    log_dir: Path = field(default_factory=lambda: Path("logs"))
    database_path: Path = field(default_factory=lambda: Path("runtime") / "apex_memory.sqlite3")
    state_path: Path = field(default_factory=lambda: Path("runtime") / "state.json")
    kill_switch_path: Path = field(default_factory=lambda: Path("KILL_SWITCH"))
    dashboard_enabled: bool = True
    dashboard_host: str = "127.0.0.1"
    dashboard_port: int = 8088
    x_bearer_token: str = ""
    user_agent: str = "ApexAutoTrader/1.0"
    risk: RiskConfig = field(default_factory=RiskConfig)

    @property
    def live_trading_enabled(self) -> bool:
        return self.trading_mode == "live" and self.autonomous_ack == LIVE_ACK_PHRASE

    @property
    def all_symbols(self) -> list[str]:
        symbols = list(dict.fromkeys(self.watchlist + self.crypto_watchlist))
        return symbols

    def ensure_directories(self) -> None:
        self.data_dir.mkdir(parents=True, exist_ok=True)
        self.log_dir.mkdir(parents=True, exist_ok=True)
        self.database_path.parent.mkdir(parents=True, exist_ok=True)

    def validate(self) -> None:
        if not self.openai_api_key:
            raise ValueError("OPENAI_API_KEY is required")
        if self.trading_mode not in {"dry_run", "live"}:
            raise ValueError("TRADING_MODE must be dry_run or live")
        if not 5 <= self.loop_interval_minutes <= 30:
            raise ValueError("LOOP_INTERVAL_MINUTES must be between 5 and 30")
        if self.mcp_transport not in {"stdio", "streamable_http"}:
            raise ValueError("ROBINHOOD_MCP_TRANSPORT must be stdio or streamable_http")
        if self.trading_mode == "live" and self.autonomous_ack != LIVE_ACK_PHRASE:
            raise ValueError(
                f"Live autonomous trading requires AUTONOMOUS_TRADING_ACK={LIVE_ACK_PHRASE!r}"
            )
        if not self.risk.allow_fractional and self.risk.min_order_notional < 5:
            raise ValueError("min_order_notional should be higher when fractional trading is disabled")
        self.risk.validate()


def load_config(config_path: str | Path | None = None) -> AppConfig:
    load_dotenv()

    path = Path(config_path or os.getenv("APEX_CONFIG_PATH", "config.json"))
    raw = _read_json(path)
    risk_raw = raw.get("risk", {})

    mcp_url = str(raw.get("mcp_url", os.getenv("ROBINHOOD_MCP_URL", AppConfig.mcp_url)))
    mcp_args_raw = raw.get("mcp_args")
    if mcp_args_raw is None:
        mcp_args = os.getenv("ROBINHOOD_MCP_ARGS")
        if mcp_args:
            mcp_args_list = mcp_args.split()
        else:
            mcp_args_list = ["-y", "mcp-remote@latest", mcp_url]
    else:
        mcp_args_list = [str(arg) for arg in mcp_args_raw]

    cfg = AppConfig(
        openai_api_key=str(raw.get("openai_api_key", os.getenv("OPENAI_API_KEY", ""))),
        openai_model=str(raw.get("openai_model", os.getenv("OPENAI_MODEL", "gpt-5.5"))),
        openai_reasoning_effort=str(
            raw.get("openai_reasoning_effort", os.getenv("OPENAI_REASONING_EFFORT", "medium"))
        ),
        openai_max_output_tokens=int(raw.get("openai_max_output_tokens", os.getenv("OPENAI_MAX_OUTPUT_TOKENS", 4000))),
        mcp_url=mcp_url,
        mcp_transport=str(raw.get("mcp_transport", os.getenv("ROBINHOOD_MCP_TRANSPORT", "stdio"))),
        mcp_command=str(raw.get("mcp_command", os.getenv("ROBINHOOD_MCP_COMMAND", "npx"))),
        mcp_args=mcp_args_list,
        mcp_timeout_seconds=int(raw.get("mcp_timeout_seconds", os.getenv("MCP_TIMEOUT_SECONDS", 45))),
        trading_mode=str(raw.get("trading_mode", os.getenv("TRADING_MODE", "dry_run"))).lower(),
        autonomous_ack=str(raw.get("autonomous_ack", os.getenv("AUTONOMOUS_TRADING_ACK", ""))),
        loop_interval_minutes=int(raw.get("loop_interval_minutes", os.getenv("LOOP_INTERVAL_MINUTES", 15))),
        loop_jitter_seconds=int(raw.get("loop_jitter_seconds", os.getenv("LOOP_JITTER_SECONDS", 120))),
        watchlist=_parse_list(raw.get("watchlist", os.getenv("WATCHLIST")), AppConfig().watchlist if False else ["SPY", "QQQ", "AAPL", "MSFT", "NVDA", "AMZN", "META"]),
        crypto_watchlist=_parse_list(raw.get("crypto_watchlist", os.getenv("CRYPTO_WATCHLIST")), ["BTC-USD", "ETH-USD"]),
        news_queries=_parse_list(raw.get("news_queries", os.getenv("NEWS_QUERIES")), ["market moving news", "federal reserve rates", "stock market"]),
        data_dir=Path(raw.get("data_dir", os.getenv("DATA_DIR", "runtime"))),
        log_dir=Path(raw.get("log_dir", os.getenv("LOG_DIR", "logs"))),
        database_path=Path(raw.get("database_path", os.getenv("DATABASE_PATH", "runtime/apex_memory.sqlite3"))),
        state_path=Path(raw.get("state_path", os.getenv("STATE_PATH", "runtime/state.json"))),
        kill_switch_path=Path(raw.get("kill_switch_path", os.getenv("KILL_SWITCH_PATH", "KILL_SWITCH"))),
        dashboard_enabled=_parse_bool(raw.get("dashboard_enabled", os.getenv("DASHBOARD_ENABLED")), True),
        dashboard_host=str(raw.get("dashboard_host", os.getenv("DASHBOARD_HOST", "127.0.0.1"))),
        dashboard_port=int(raw.get("dashboard_port", os.getenv("DASHBOARD_PORT", 8088))),
        x_bearer_token=str(raw.get("x_bearer_token", os.getenv("X_BEARER_TOKEN", ""))),
        user_agent=str(raw.get("user_agent", os.getenv("USER_AGENT", "ApexAutoTrader/1.0"))),
        risk=RiskConfig.from_dict(risk_raw),
    )
    cfg.ensure_directories()
    cfg.validate()
    return cfg
