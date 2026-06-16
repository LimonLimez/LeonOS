from __future__ import annotations

import json
import logging
from typing import Any

from openai import APIError, AsyncOpenAI

from config import AppConfig


LOGGER = logging.getLogger("apex.llm")


TRADE_DECISION_SCHEMA: dict[str, Any] = {
    "type": "object",
    "properties": {
        "market_regime": {"type": "string"},
        "portfolio_assessment": {
            "type": "object",
            "properties": {
                "risk_level": {"type": "string", "enum": ["low", "medium", "high", "critical"]},
                "drawdown_comment": {"type": "string"},
                "cash_comment": {"type": "string"},
                "concentration_comment": {"type": "string"},
            },
            "required": ["risk_level", "drawdown_comment", "cash_comment", "concentration_comment"],
            "additionalProperties": False,
        },
        "decisions": {
            "type": "array",
            "items": {
                "type": "object",
                "properties": {
                    "action": {"type": "string", "enum": ["buy", "sell", "hold"]},
                    "symbol": {"type": "string"},
                    "asset_type": {"type": "string", "enum": ["stock", "crypto", "cash"]},
                    "order_type": {"type": "string", "enum": ["market", "limit", "none"]},
                    "notional_usd": {"type": "number", "minimum": 0},
                    "quantity": {"type": ["number", "null"], "minimum": 0},
                    "limit_price": {"type": ["number", "null"], "minimum": 0},
                    "confidence": {"type": "number", "minimum": 0, "maximum": 1},
                    "probability_of_profit": {"type": "number", "minimum": 0, "maximum": 1},
                    "risk_reward": {"type": "string"},
                    "rationale": {"type": "string"},
                    "stop_loss_pct": {"type": "number", "minimum": 0},
                    "take_profit_pct": {"type": "number", "minimum": 0},
                    "time_horizon": {"type": "string"},
                    "conditions": {"type": "string"},
                },
                "required": [
                    "action",
                    "symbol",
                    "asset_type",
                    "order_type",
                    "notional_usd",
                    "quantity",
                    "limit_price",
                    "confidence",
                    "probability_of_profit",
                    "risk_reward",
                    "rationale",
                    "stop_loss_pct",
                    "take_profit_pct",
                    "time_horizon",
                    "conditions",
                ],
                "additionalProperties": False,
            },
        },
        "lessons_to_log": {"type": "array", "items": {"type": "string"}},
        "should_pause_new_buys": {"type": "boolean"},
        "summary": {"type": "string"},
    },
    "required": ["market_regime", "portfolio_assessment", "decisions", "lessons_to_log", "should_pause_new_buys", "summary"],
    "additionalProperties": False,
}


REFLECTION_SCHEMA: dict[str, Any] = {
    "type": "object",
    "properties": {
        "lesson": {"type": "string"},
        "mistakes_or_risks": {"type": "array", "items": {"type": "string"}},
        "strategy_weight_adjustments": {
            "type": "object",
            "properties": {
                "momentum": {"type": "number", "minimum": -0.25, "maximum": 0.25},
                "mean_reversion": {"type": "number", "minimum": -0.25, "maximum": 0.25},
                "news_sentiment": {"type": "number", "minimum": -0.25, "maximum": 0.25},
                "risk_off": {"type": "number", "minimum": -0.25, "maximum": 0.25},
                "cash_bias": {"type": "number", "minimum": -0.25, "maximum": 0.25},
            },
            "required": ["momentum", "mean_reversion", "news_sentiment", "risk_off", "cash_bias"],
            "additionalProperties": False,
        },
        "next_cycle_focus": {"type": "array", "items": {"type": "string"}},
        "should_pause": {"type": "boolean"},
    },
    "required": ["lesson", "mistakes_or_risks", "strategy_weight_adjustments", "next_cycle_focus", "should_pause"],
    "additionalProperties": False,
}


SYSTEM_PROMPT = """You are ApexAutoTrader's trading decision engine.

Non-negotiable operating policy:
- Capital preservation comes first. Avoid trades when evidence quality is weak.
- You are not allowed to recommend options, margin, leverage, short selling, or revenge trading.
- Respect hard risk limits supplied in the context. The Python engine will reject unsafe output.
- Prefer fewer, higher-conviction trades over activity for its own sake.
- Treat news, social sentiment, and price momentum as noisy. Explain uncertainty in concise rationale fields.
- If drawdown, liquidity, missing market data, or MCP/account uncertainty is material, hold or sell to reduce risk.
- For each buy/sell, include a notional USD size, stop-loss percent, and take-profit percent.
- Do not include hidden chain-of-thought. Use short, auditable rationales only.
"""


class LLMReasoner:
    def __init__(self, config: AppConfig):
        self.config = config
        self.client = AsyncOpenAI(api_key=config.openai_api_key)

    async def generate_trade_plan(self, context: dict[str, Any]) -> dict[str, Any]:
        prompt = (
            "Analyze the current autonomous trading context and return a conservative trade plan. "
            "Use only the supplied data and explicitly prefer hold when the data does not justify action.\n\n"
            f"Context JSON:\n{json.dumps(context, default=str, sort_keys=True)}"
        )
        try:
            return await self._structured_response(
                name="apex_trade_decision",
                schema=TRADE_DECISION_SCHEMA,
                prompt=prompt,
            )
        except Exception as exc:
            LOGGER.exception("LLM trade plan failed; falling back to hold-only plan")
            return {
                "market_regime": "unknown",
                "portfolio_assessment": {
                    "risk_level": "critical",
                    "drawdown_comment": "LLM call failed.",
                    "cash_comment": "No action until reasoning recovers.",
                    "concentration_comment": "Unknown.",
                },
                "decisions": [
                    {
                        "action": "hold",
                        "symbol": "CASH",
                        "asset_type": "cash",
                        "order_type": "none",
                        "notional_usd": 0,
                        "quantity": None,
                        "limit_price": None,
                        "confidence": 0,
                        "probability_of_profit": 0,
                        "risk_reward": "No trade because LLM failed.",
                        "rationale": str(exc),
                        "stop_loss_pct": 0,
                        "take_profit_pct": 0,
                        "time_horizon": "none",
                        "conditions": "Wait for next cycle.",
                    }
                ],
                "lessons_to_log": ["LLM failure caused a no-trade fallback."],
                "should_pause_new_buys": True,
                "summary": "No trade. LLM reasoning failed.",
            }

    async def critique_and_reflect(self, context: dict[str, Any]) -> dict[str, Any]:
        prompt = (
            "Critique the latest trading cycle and update strategy weights cautiously. "
            "Use tiny changes only; do not overfit to one outcome.\n\n"
            f"Context JSON:\n{json.dumps(context, default=str, sort_keys=True)}"
        )
        try:
            return await self._structured_response(
                name="apex_reflection",
                schema=REFLECTION_SCHEMA,
                prompt=prompt,
            )
        except Exception as exc:
            LOGGER.exception("LLM reflection failed; using neutral reflection")
            return {
                "lesson": f"Reflection failed: {exc}",
                "mistakes_or_risks": ["No strategy update was applied."],
                "strategy_weight_adjustments": {
                    "momentum": 0,
                    "mean_reversion": 0,
                    "news_sentiment": 0,
                    "risk_off": 0,
                    "cash_bias": 0,
                },
                "next_cycle_focus": ["Recover normal reflection workflow."],
                "should_pause": False,
            }

    async def _structured_response(self, name: str, schema: dict[str, Any], prompt: str) -> dict[str, Any]:
        kwargs: dict[str, Any] = {
            "model": self.config.openai_model,
            "input": [
                {"role": "system", "content": SYSTEM_PROMPT},
                {"role": "user", "content": prompt},
            ],
            "text": {
                "format": {
                    "type": "json_schema",
                    "name": name,
                    "strict": True,
                    "schema": schema,
                },
                "verbosity": "low",
            },
            "max_output_tokens": self.config.openai_max_output_tokens,
        }
        if _supports_reasoning(self.config.openai_model):
            kwargs["reasoning"] = {"effort": self.config.openai_reasoning_effort}

        response = await self.client.responses.create(**kwargs)
        if getattr(response, "status", None) == "incomplete":
            details = getattr(response, "incomplete_details", None)
            raise APIError(f"Incomplete OpenAI response: {details}", request=None, body=None)

        output_text = _extract_output_text(response)
        return json.loads(output_text)


def _supports_reasoning(model: str) -> bool:
    normalized = model.lower()
    return normalized.startswith("o") or "gpt-5" in normalized


def _extract_output_text(response: Any) -> str:
    output_text = getattr(response, "output_text", None)
    if output_text:
        return str(output_text)

    for item in getattr(response, "output", []) or []:
        if getattr(item, "type", None) != "message":
            continue
        for content in getattr(item, "content", []) or []:
            content_type = getattr(content, "type", None)
            if content_type == "refusal":
                raise RuntimeError(getattr(content, "refusal", "Model refused the request"))
            if content_type == "output_text":
                return str(getattr(content, "text", ""))
    raise RuntimeError("OpenAI response did not contain output_text")
