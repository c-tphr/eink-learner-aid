"""Report output: index.html, results.json and junit.xml in the report folder."""

from __future__ import annotations

import html
import json
import time
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Any

from .runner import ScenarioResult

CSS = """
:root { --fg:#1b1b1b; --bg:#fff; --muted:#666; --line:#ddd; --pass:#1a7f37; --fail:#c62828; --skip:#8a6d00;
        --code:#f5f5f5; }
@media (prefers-color-scheme: dark) { :root { --fg:#e6e6e6; --bg:#151515; --muted:#9a9a9a; --line:#333;
        --pass:#57c26c; --fail:#ff6b6b; --skip:#e0b400; --code:#1f1f1f; } }
body { font: 14px/1.45 system-ui, sans-serif; color: var(--fg); background: var(--bg); margin: 0 auto;
       max-width: 1100px; padding: 16px; }
h1 { font-size: 20px; } h2 { font-size: 17px; margin-top: 32px; border-top: 1px solid var(--line);
       padding-top: 16px; }
table { border-collapse: collapse; width: 100%; } td, th { border-bottom: 1px solid var(--line);
       padding: 4px 6px; text-align: left; vertical-align: top; }
.pass { color: var(--pass); font-weight: 600; } .fail { color: var(--fail); font-weight: 600; }
.skipped { color: var(--skip); font-weight: 600; }
code, pre { background: var(--code); font: 12px/1.4 ui-monospace, monospace; }
pre { padding: 8px; overflow-x: auto; white-space: pre-wrap; word-break: break-all; max-height: 480px; }
.shots { display: flex; flex-wrap: wrap; gap: 12px; } .shots figure { margin: 0; }
.shots img { width: 240px; border: 1px solid var(--line); image-rendering: pixelated; }
.diff img { max-width: 100%; border: 1px solid var(--line); }
figcaption { color: var(--muted); font-size: 12px; } .muted { color: var(--muted); }
"""


def write_all(results: list[ScenarioResult], report_dir: Path, meta: dict[str, Any]) -> None:
    report_dir.mkdir(parents=True, exist_ok=True)
    (report_dir / "results.json").write_text(json.dumps(results_json(results, meta), indent=2) + "\n",
                                             encoding="utf-8")
    (report_dir / "junit.xml").write_text(junit_xml(results), encoding="utf-8")
    (report_dir / "index.html").write_text(index_html(results, meta, report_dir), encoding="utf-8")


def results_json(results: list[ScenarioResult], meta: dict[str, Any]) -> dict[str, Any]:
    return {
        "meta": meta,
        "passed": sum(r.status == "pass" for r in results),
        "failed": sum(r.status != "pass" for r in results),
        "scenarios": [r.to_json() for r in results],
    }


def junit_xml(results: list[ScenarioResult]) -> str:
    suite = ET.Element("testsuite", name="simtest", tests=str(len(results)),
                       failures=str(sum(r.status != "pass" for r in results)), errors="0",
                       time=f"{sum(r.duration for r in results):.3f}",
                       timestamp=time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime()))
    for r in results:
        case = ET.SubElement(suite, "testcase", classname="simtest", name=r.label, time=f"{r.duration:.3f}")
        if r.status != "pass":
            failed = [s for s in r.steps if s.status == "fail"]
            if r.error:
                first = r.error.splitlines()[0]
            else:
                first = failed[0].message.splitlines()[0] if failed and failed[0].message else "failed"
            body = r.error or "\n".join(s.message for s in failed)
            for s in failed:
                if s.message and s.message not in body:
                    body += "\n" + s.message
            fail = ET.SubElement(case, "failure", message=first[:500])
            fail.text = body
        out = ET.SubElement(case, "system-out")
        out.text = "\n".join(f"step {s.index} {s.action}: {s.status} {s.message.splitlines()[0] if s.message else ''}"
                             for s in r.steps)
    ET.indent(suite)
    return '<?xml version="1.0" encoding="UTF-8"?>\n' + ET.tostring(suite, encoding="unicode") + "\n"


def _e(s: Any) -> str:
    return html.escape(str(s), quote=True)


def index_html(results: list[ScenarioResult], meta: dict[str, Any], report_dir: Path | None = None) -> str:
    passed = sum(r.status == "pass" for r in results)
    parts = [f"<!doctype html><html lang=en><head><meta charset=utf-8>"
             f"<meta name=viewport content='width=device-width, initial-scale=1'>"
             f"<title>Simtest report</title><style>{CSS}</style></head><body>",
             f"<h1>Simulator scenarios: {passed} of {len(results)} passed</h1>",
             f"<p class=muted>Binary <code>{_e(meta.get('binary', ''))}</code>, started "
             f"{_e(meta.get('started', ''))}, {meta.get('duration', 0):.1f} s. "
             f"Also: <a href=results.json>results.json</a>, <a href=junit.xml>junit.xml</a>.</p>",
             "<table><tr><th>Scenario</th><th>Result</th><th>Time</th><th>Description</th></tr>"]
    for r in results:
        parts.append(f"<tr><td><a href='#{_e(r.label)}'>{_e(r.label)}</a></td>"
                     f"<td class={_e(r.status)}>{_e(r.status.upper())}</td><td>{r.duration:.1f} s</td>"
                     f"<td>{_e(r.description)}</td></tr>")
    parts.append("</table>")
    for r in results:
        parts.append(_scenario_html(r, report_dir))
    parts.append("</body></html>\n")
    return "".join(parts)


def _scenario_html(r: ScenarioResult, report_dir: Path | None) -> str:
    p = [f"<h2 id='{_e(r.label)}'>{_e(r.label)} <span class={_e(r.status)}>{_e(r.status.upper())}</span></h2>",
         f"<p>{_e(r.description)}<br><span class=muted>{_e(r.scenario_file)}, {r.duration:.1f} s, "
         f"SD folder <code>{_e(r.sd_path)}</code> {'(kept)' if r.sd_kept else '(deleted)'}</span></p>"]
    if r.error:
        p.append(f"<pre class=fail>{_e(r.error)}</pre>")
    if r.refs_updated:
        p.append("<p>References updated:</p><ul>" + "".join(f"<li>{_e(x)}</li>" for x in r.refs_updated)
                 + "</ul>")
    p.append("<table><tr><th>#</th><th>Step</th><th>Run</th><th>Result</th><th>At</th><th>Took</th>"
             "<th>Detail</th></tr>")
    for s in r.steps:
        msg = s.message.splitlines()[0] if s.message else ""
        p.append(f"<tr><td>{s.index}</td><td><code>{_e(s.line)}</code></td><td>{_e(s.run or '')}</td>"
                 f"<td class={_e(s.status)}>{_e(s.status)}</td>"
                 f"<td>{'' if s.t is None else f'{s.t:.2f} s'}</td>"
                 f"<td>{'' if s.duration is None else f'{s.duration:.2f} s'}</td><td>{_e(msg)}</td></tr>")
    p.append("</table>")
    diffs = [s for s in r.steps if "diff" in s.images]
    for s in diffs:
        p.append(f"<h3>Mismatch: {_e(s.line)}</h3><p class=fail>{_e(s.message)}</p>"
                 f"<figure class=diff><img src='{_e(s.images['diff'])}' alt='diff'>"
                 f"<figcaption>reference | actual | diff &mdash; files: "
                 f"<a href='{_e(s.images['reference'])}'>reference</a>, "
                 f"<a href='{_e(s.images['actual'])}'>actual</a></figcaption></figure>")
    if r.shots:
        p.append("<h3>Screenshots</h3><div class=shots>")
        for name, shot in r.shots.items():
            p.append(f"<figure><a href='{_e(shot['png'])}'><img src='{_e(shot['png'])}' alt='{_e(name)}'></a>"
                     f"<figcaption>{_e(name)}<br><code>{_e(shot['sha256'][:12])}</code></figcaption></figure>")
        p.append("</div>")
    p.append("<h3>Runs</h3><ul>" + "".join(
        f"<li>run {_e(x['number'])}: pid {_e(x['pid'])}, ended by {_e(x['end'])}, exit {_e(x['exit'])}</li>"
        for x in r.runs) + "</ul>")
    log_text = ""
    if report_dir is not None and r.log_file:
        try:
            log_text = (report_dir / r.log_file).read_text(encoding="utf-8")
        except OSError:
            log_text = "(log file missing)"
    open_log = " open" if r.status != "pass" else ""
    p.append(f"<details{open_log}><summary>Simulator and harness log "
             f"(<a href='{_e(r.log_file)}'>log.txt</a>)</summary><pre>{_e(log_text)}</pre></details>")
    p.append("<details><summary>SD card after the run</summary><table><tr><th>Path</th><th>Bytes</th>"
             "<th>Contents</th></tr>")
    for f in r.sd_listing:
        content = ""
        if "text" in f:
            content = f"<pre>{_e(f['text'])}</pre>"
        elif "hex" in f:
            content = f"<code>{_e(f['hex'])}</code>"
        p.append(f"<tr><td><code>{_e(f['path'])}</code></td><td>{'' if f['size'] is None else f['size']}</td>"
                 f"<td>{content}</td></tr>")
    p.append("</table></details>")
    return "".join(p)
