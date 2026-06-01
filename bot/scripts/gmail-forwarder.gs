/*
 * gmail-forwarder.gs — Google Apps Script that forwards Lakeside-style
 * emails from your Gmail account into the papa-watch family-events
 * /ingest endpoint, so Gemini Phase 2 can extract real-world events
 * instead of curl'd fakes.
 *
 * ─── SETUP (one-time, ~5 minutes) ──────────────────────────────────────
 *
 * 1. In Gmail, create a filter:
 *      Has the words:  from:(@lakesidelosgatos.com)
 *      Action:         Apply label  →  papa-fwd
 *    (Create the label first via Settings → Labels → Create new label.)
 *    Optional: tick "Also apply to N matching conversations" to backfill
 *    existing Lakeside emails.
 *
 * 2. Visit https://script.google.com → "New project". Paste this entire
 *    file as the content of `Code.gs`. Save (⌘S).
 *
 * 3. Project Settings (gear icon, left rail) → "Script properties" →
 *    "Add script property", three rows:
 *        INGEST_URL    https://papa-watch-bot.su-wakeup.workers.dev/ingest/<INGEST_TOKEN>/email
 *        FWD_LABEL     papa-fwd
 *        DONE_LABEL    papa-fwd-done
 *    Substitute your real INGEST_TOKEN secret in the URL.
 *
 * 4. Back in the editor, drop-down "Select function" → forwardOnce,
 *    click Run. The first run pops an OAuth consent screen — grant
 *    Gmail read + outbound URL fetch. This authorizes the script.
 *
 * 5. Triggers (clock icon, left rail) → "Add trigger":
 *        Choose function:   forwardOnce
 *        Event source:      Time-driven
 *        Time-based type:   Minutes timer
 *        Minute interval:   Every 10 minutes
 *    Save. Done.
 *
 * Now every 10 minutes the script scans for new `papa-fwd` emails that
 * haven't been forwarded yet, POSTs each to /ingest, and tags them
 * `papa-fwd-done` so they're never re-sent. Phase 2 extraction and
 * Phase 3 fan-out then run automatically server-side.
 *
 * ─── DEBUGGING ────────────────────────────────────────────────────────
 *
 * - In the Apps Script editor, "Executions" (left rail) shows each run
 *   and its Logger output.
 * - Run testIngest() to verify connectivity without sending real mail.
 * - If a forward fails (5xx, 400, etc), the email keeps its `papa-fwd`
 *   label and the script retries next tick.
 */

const PROPS = PropertiesService.getScriptProperties();

function getOrCreateLabel_(name) {
    return GmailApp.getUserLabelByName(name) || GmailApp.createLabel(name);
}

function forwardOnce() {
    const ingestUrl     = PROPS.getProperty("INGEST_URL");
    const fwdLabelName  = PROPS.getProperty("FWD_LABEL")  || "papa-fwd";
    const doneLabelName = PROPS.getProperty("DONE_LABEL") || "papa-fwd-done";

    if (!ingestUrl) {
        Logger.log("FATAL: script property INGEST_URL is unset");
        return;
    }

    const fwdLabel = GmailApp.getUserLabelByName(fwdLabelName);
    if (!fwdLabel) {
        Logger.log("FATAL: Gmail label '" + fwdLabelName + "' not found — "
                 + "create it and the filter that applies it first");
        return;
    }
    const doneLabel = getOrCreateLabel_(doneLabelName);

    const query = "label:" + fwdLabelName + " -label:" + doneLabelName;
    const threads = GmailApp.search(query, 0, 25);
    Logger.log("found " + threads.length + " thread(s) to forward");

    let okCount = 0, failCount = 0;
    for (const thread of threads) {
        let threadHadFailure = false;
        for (const msg of thread.getMessages()) {
            try {
                const body = msg.getPlainBody() || msg.getBody() || "";
                const payload = {
                    from:        msg.getFrom(),
                    to:          msg.getTo(),
                    subject:     msg.getSubject(),
                    received_at: Math.floor(msg.getDate().getTime() / 1000),
                    body:        body,
                };
                const resp = UrlFetchApp.fetch(ingestUrl, {
                    method:             "post",
                    contentType:        "application/json",
                    payload:            JSON.stringify(payload),
                    muteHttpExceptions: true,
                });
                const code = resp.getResponseCode();
                if (code === 200) {
                    okCount++;
                    Logger.log("OK   " + msg.getSubject());
                } else {
                    failCount++;
                    threadHadFailure = true;
                    Logger.log("FAIL " + code + " on '" + msg.getSubject() + "': "
                             + resp.getContentText().substring(0, 200));
                }
            } catch (e) {
                failCount++;
                threadHadFailure = true;
                Logger.log("ERR  " + e.message + " on " + msg.getSubject());
            }
        }

        // Only mark the thread done if every message in it forwarded
        // cleanly — otherwise the next tick retries.
        if (!threadHadFailure) {
            thread.removeLabel(fwdLabel);
            thread.addLabel(doneLabel);
        }
    }

    Logger.log("summary: ok=" + okCount + " fail=" + failCount);
}

// Smoke test — POSTs a hand-rolled "Lakeside" payload to /ingest so you
// can verify the URL + token work without waiting for real mail. Run
// manually from the editor.
function testIngest() {
    const ingestUrl = PROPS.getProperty("INGEST_URL");
    if (!ingestUrl) {
        Logger.log("FATAL: script property INGEST_URL is unset");
        return;
    }
    const payload = {
        from:        "office@lakesidelosgatos.com",
        subject:     "Apps Script test forward",
        received_at: Math.floor(Date.now() / 1000),
        body:        "This is a smoke test POSTed from the Gmail Apps Script "
                   + "forwarder. If you see this row in the admin /admin UI, "
                   + "the URL + token pair is wired correctly.",
    };
    const resp = UrlFetchApp.fetch(ingestUrl, {
        method:             "post",
        contentType:        "application/json",
        payload:            JSON.stringify(payload),
        muteHttpExceptions: true,
    });
    Logger.log(resp.getResponseCode() + ": " + resp.getContentText());
}
