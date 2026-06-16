from __future__ import annotations

import asyncio
import json
import sqlite3
from dataclasses import dataclass
from datetime import UTC, datetime, timedelta
from pathlib import Path
from typing import Any


def utc_now() -> str:
    return datetime.now(UTC).isoformat()


def _json(data: Any) -> str:
    return json.dumps(data, sort_keys=True, default=str)


def _loads(value: str | None, default: Any = None) -> Any:
    if value is None:
        return default
    try:
        return json.loads(value)
    except json.JSONDecodeError:
        return default


@dataclass(slots=True)
class CycleRecord:
    id: int
    started_at: str


class TradingMemory:
    def __init__(self, database_path: Path):
        self.database_path = Path(database_path)

    async def initialize(self) -> None:
        await asyncio.to_thread(self._initialize_sync)

    def _connect(self) -> sqlite3.Connection:
        self.database_path.parent.mkdir(parents=True, exist_ok=True)
        conn = sqlite3.connect(self.database_path, timeout=30)
        conn.row_factory = sqlite3.Row
        conn.execute("PRAGMA journal_mode=WAL")
        conn.execute("PRAGMA foreign_keys=ON")
        return conn

    def _initialize_sync(self) -> None:
        with self._connect() as conn:
            conn.executescript(
                """
                CREATE TABLE IF NOT EXISTS metadata (
                    key TEXT PRIMARY KEY,
                    value_json TEXT NOT NULL,
                    updated_at TEXT NOT NULL
                );

                CREATE TABLE IF NOT EXISTS cycles (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    started_at TEXT NOT NULL,
                    finished_at TEXT,
                    status TEXT NOT NULL,
                    summary_json TEXT,
                    error TEXT
                );

                CREATE TABLE IF NOT EXISTS account_snapshots (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    cycle_id INTEGER,
                    captured_at TEXT NOT NULL,
                    equity REAL,
                    buying_power REAL,
                    cash REAL,
                    drawdown REAL,
                    raw_json TEXT NOT NULL,
                    FOREIGN KEY(cycle_id) REFERENCES cycles(id)
                );

                CREATE TABLE IF NOT EXISTS decisions (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    cycle_id INTEGER,
                    created_at TEXT NOT NULL,
                    symbol TEXT,
                    asset_type TEXT,
                    action TEXT NOT NULL,
                    confidence REAL,
                    notional REAL,
                    rationale TEXT,
                    approved INTEGER NOT NULL,
                    rejection_reason TEXT,
                    raw_json TEXT NOT NULL,
                    FOREIGN KEY(cycle_id) REFERENCES cycles(id)
                );

                CREATE TABLE IF NOT EXISTS orders (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    cycle_id INTEGER,
                    created_at TEXT NOT NULL,
                    symbol TEXT NOT NULL,
                    asset_type TEXT,
                    side TEXT NOT NULL,
                    order_type TEXT,
                    notional REAL,
                    quantity REAL,
                    status TEXT NOT NULL,
                    live_order INTEGER NOT NULL,
                    broker_order_id TEXT,
                    raw_json TEXT,
                    error TEXT,
                    FOREIGN KEY(cycle_id) REFERENCES cycles(id)
                );

                CREATE TABLE IF NOT EXISTS reflections (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    cycle_id INTEGER,
                    created_at TEXT NOT NULL,
                    lesson TEXT NOT NULL,
                    adjustments_json TEXT NOT NULL,
                    raw_json TEXT NOT NULL,
                    FOREIGN KEY(cycle_id) REFERENCES cycles(id)
                );

                CREATE TABLE IF NOT EXISTS strategy_weights (
                    key TEXT PRIMARY KEY,
                    value REAL NOT NULL,
                    updated_at TEXT NOT NULL
                );
                """
            )
            defaults = {
                "momentum": 1.0,
                "mean_reversion": 1.0,
                "news_sentiment": 1.0,
                "risk_off": 1.0,
                "cash_bias": 1.0,
            }
            for key, value in defaults.items():
                conn.execute(
                    """
                    INSERT OR IGNORE INTO strategy_weights (key, value, updated_at)
                    VALUES (?, ?, ?)
                    """,
                    (key, value, utc_now()),
                )

    async def start_cycle(self) -> CycleRecord:
        return await asyncio.to_thread(self._start_cycle_sync)

    def _start_cycle_sync(self) -> CycleRecord:
        started_at = utc_now()
        with self._connect() as conn:
            cursor = conn.execute(
                "INSERT INTO cycles (started_at, status) VALUES (?, ?)",
                (started_at, "running"),
            )
            return CycleRecord(id=int(cursor.lastrowid), started_at=started_at)

    async def finish_cycle(self, cycle_id: int, status: str, summary: dict[str, Any] | None = None, error: str | None = None) -> None:
        await asyncio.to_thread(self._finish_cycle_sync, cycle_id, status, summary or {}, error)

    def _finish_cycle_sync(self, cycle_id: int, status: str, summary: dict[str, Any], error: str | None) -> None:
        with self._connect() as conn:
            conn.execute(
                """
                UPDATE cycles
                SET finished_at = ?, status = ?, summary_json = ?, error = ?
                WHERE id = ?
                """,
                (utc_now(), status, _json(summary), error, cycle_id),
            )

    async def record_account_snapshot(
        self,
        cycle_id: int,
        equity: float | None,
        buying_power: float | None,
        cash: float | None,
        drawdown: float | None,
        raw: dict[str, Any],
    ) -> None:
        await asyncio.to_thread(self._record_account_snapshot_sync, cycle_id, equity, buying_power, cash, drawdown, raw)

    def _record_account_snapshot_sync(
        self,
        cycle_id: int,
        equity: float | None,
        buying_power: float | None,
        cash: float | None,
        drawdown: float | None,
        raw: dict[str, Any],
    ) -> None:
        with self._connect() as conn:
            conn.execute(
                """
                INSERT INTO account_snapshots (cycle_id, captured_at, equity, buying_power, cash, drawdown, raw_json)
                VALUES (?, ?, ?, ?, ?, ?, ?)
                """,
                (cycle_id, utc_now(), equity, buying_power, cash, drawdown, _json(raw)),
            )

    async def record_decision(
        self,
        cycle_id: int,
        decision: dict[str, Any],
        approved: bool,
        rejection_reason: str | None,
    ) -> None:
        await asyncio.to_thread(self._record_decision_sync, cycle_id, decision, approved, rejection_reason)

    def _record_decision_sync(
        self,
        cycle_id: int,
        decision: dict[str, Any],
        approved: bool,
        rejection_reason: str | None,
    ) -> None:
        with self._connect() as conn:
            conn.execute(
                """
                INSERT INTO decisions
                    (cycle_id, created_at, symbol, asset_type, action, confidence, notional, rationale,
                     approved, rejection_reason, raw_json)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    cycle_id,
                    utc_now(),
                    decision.get("symbol"),
                    decision.get("asset_type"),
                    decision.get("action", "hold"),
                    _safe_float(decision.get("confidence")),
                    _safe_float(decision.get("notional_usd")),
                    decision.get("rationale"),
                    1 if approved else 0,
                    rejection_reason,
                    _json(decision),
                ),
            )

    async def record_order(
        self,
        cycle_id: int,
        symbol: str,
        asset_type: str | None,
        side: str,
        order_type: str | None,
        notional: float | None,
        quantity: float | None,
        status: str,
        live_order: bool,
        broker_order_id: str | None = None,
        raw: dict[str, Any] | None = None,
        error: str | None = None,
    ) -> None:
        await asyncio.to_thread(
            self._record_order_sync,
            cycle_id,
            symbol,
            asset_type,
            side,
            order_type,
            notional,
            quantity,
            status,
            live_order,
            broker_order_id,
            raw or {},
            error,
        )

    def _record_order_sync(
        self,
        cycle_id: int,
        symbol: str,
        asset_type: str | None,
        side: str,
        order_type: str | None,
        notional: float | None,
        quantity: float | None,
        status: str,
        live_order: bool,
        broker_order_id: str | None,
        raw: dict[str, Any],
        error: str | None,
    ) -> None:
        with self._connect() as conn:
            conn.execute(
                """
                INSERT INTO orders
                    (cycle_id, created_at, symbol, asset_type, side, order_type, notional, quantity,
                     status, live_order, broker_order_id, raw_json, error)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    cycle_id,
                    utc_now(),
                    symbol,
                    asset_type,
                    side,
                    order_type,
                    notional,
                    quantity,
                    status,
                    1 if live_order else 0,
                    broker_order_id,
                    _json(raw),
                    error,
                ),
            )

    async def record_reflection(self, cycle_id: int, reflection: dict[str, Any]) -> None:
        await asyncio.to_thread(self._record_reflection_sync, cycle_id, reflection)

    def _record_reflection_sync(self, cycle_id: int, reflection: dict[str, Any]) -> None:
        adjustments = reflection.get("strategy_weight_adjustments", {})
        with self._connect() as conn:
            conn.execute(
                """
                INSERT INTO reflections (cycle_id, created_at, lesson, adjustments_json, raw_json)
                VALUES (?, ?, ?, ?, ?)
                """,
                (
                    cycle_id,
                    utc_now(),
                    reflection.get("lesson", ""),
                    _json(adjustments),
                    _json(reflection),
                ),
            )

    async def get_strategy_weights(self) -> dict[str, float]:
        return await asyncio.to_thread(self._get_strategy_weights_sync)

    def _get_strategy_weights_sync(self) -> dict[str, float]:
        with self._connect() as conn:
            rows = conn.execute("SELECT key, value FROM strategy_weights ORDER BY key").fetchall()
            return {str(row["key"]): float(row["value"]) for row in rows}

    async def update_strategy_weights(self, adjustments: dict[str, Any]) -> dict[str, float]:
        return await asyncio.to_thread(self._update_strategy_weights_sync, adjustments)

    def _update_strategy_weights_sync(self, adjustments: dict[str, Any]) -> dict[str, float]:
        with self._connect() as conn:
            current = {
                str(row["key"]): float(row["value"])
                for row in conn.execute("SELECT key, value FROM strategy_weights").fetchall()
            }
            for key, delta in adjustments.items():
                if key not in current:
                    continue
                new_value = max(0.25, min(2.5, current[key] + float(delta)))
                conn.execute(
                    """
                    UPDATE strategy_weights
                    SET value = ?, updated_at = ?
                    WHERE key = ?
                    """,
                    (new_value, utc_now(), key),
                )
                current[key] = new_value
            return current

    async def get_recent_context(self, limit: int = 12) -> dict[str, Any]:
        return await asyncio.to_thread(self._get_recent_context_sync, limit)

    def _get_recent_context_sync(self, limit: int) -> dict[str, Any]:
        with self._connect() as conn:
            decisions = [
                dict(row)
                for row in conn.execute(
                    """
                    SELECT created_at, symbol, action, confidence, notional, approved, rejection_reason, rationale
                    FROM decisions
                    ORDER BY id DESC
                    LIMIT ?
                    """,
                    (limit,),
                ).fetchall()
            ]
            orders = [
                dict(row)
                for row in conn.execute(
                    """
                    SELECT created_at, symbol, side, notional, quantity, status, live_order, error
                    FROM orders
                    ORDER BY id DESC
                    LIMIT ?
                    """,
                    (limit,),
                ).fetchall()
            ]
            reflections = [
                dict(row)
                for row in conn.execute(
                    """
                    SELECT created_at, lesson, adjustments_json
                    FROM reflections
                    ORDER BY id DESC
                    LIMIT ?
                    """,
                    (limit,),
                ).fetchall()
            ]
            return {
                "recent_decisions": decisions,
                "recent_orders": orders,
                "recent_reflections": reflections,
                "strategy_weights": self._get_strategy_weights_sync(),
            }

    async def count_orders_since(self, since: datetime) -> int:
        return await asyncio.to_thread(self._count_orders_since_sync, since)

    def _count_orders_since_sync(self, since: datetime) -> int:
        with self._connect() as conn:
            row = conn.execute(
                "SELECT COUNT(*) AS count FROM orders WHERE created_at >= ? AND status NOT IN ('rejected', 'failed')",
                (since.astimezone(UTC).isoformat(),),
            ).fetchone()
            return int(row["count"] if row else 0)

    async def get_metadata(self, key: str, default: Any = None) -> Any:
        return await asyncio.to_thread(self._get_metadata_sync, key, default)

    def _get_metadata_sync(self, key: str, default: Any) -> Any:
        with self._connect() as conn:
            row = conn.execute("SELECT value_json FROM metadata WHERE key = ?", (key,)).fetchone()
            if not row:
                return default
            return _loads(row["value_json"], default)

    async def set_metadata(self, key: str, value: Any) -> None:
        await asyncio.to_thread(self._set_metadata_sync, key, value)

    def _set_metadata_sync(self, key: str, value: Any) -> None:
        with self._connect() as conn:
            conn.execute(
                """
                INSERT INTO metadata (key, value_json, updated_at)
                VALUES (?, ?, ?)
                ON CONFLICT(key) DO UPDATE SET value_json = excluded.value_json, updated_at = excluded.updated_at
                """,
                (key, _json(value), utc_now()),
            )

    async def latest_status(self) -> dict[str, Any]:
        return await asyncio.to_thread(self._latest_status_sync)

    def _latest_status_sync(self) -> dict[str, Any]:
        with self._connect() as conn:
            cycle = conn.execute(
                "SELECT * FROM cycles ORDER BY id DESC LIMIT 1"
            ).fetchone()
            snapshot = conn.execute(
                "SELECT * FROM account_snapshots ORDER BY id DESC LIMIT 1"
            ).fetchone()
            orders = conn.execute(
                "SELECT * FROM orders ORDER BY id DESC LIMIT 20"
            ).fetchall()
            decisions = conn.execute(
                "SELECT * FROM decisions ORDER BY id DESC LIMIT 20"
            ).fetchall()
            return {
                "cycle": dict(cycle) if cycle else None,
                "snapshot": dict(snapshot) if snapshot else None,
                "orders": [dict(row) for row in orders],
                "decisions": [dict(row) for row in decisions],
                "strategy_weights": self._get_strategy_weights_sync(),
            }

    async def update_peak_equity(self, equity: float | None) -> tuple[float | None, float | None]:
        if equity is None:
            return None, None
        return await asyncio.to_thread(self._update_peak_equity_sync, equity)

    def _update_peak_equity_sync(self, equity: float) -> tuple[float, float]:
        current = self._get_metadata_sync("peak_equity", None)
        peak = max(float(current or 0), float(equity))
        drawdown = 0.0 if peak <= 0 else max(0.0, (peak - float(equity)) / peak)
        self._set_metadata_sync("peak_equity", peak)
        return peak, drawdown


def _safe_float(value: Any) -> float | None:
    if value in {None, ""}:
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None
