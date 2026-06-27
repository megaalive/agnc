import json
import urllib.error
import urllib.request

BASE = "http://127.0.0.1:4096"


def main():
    req = urllib.request.Request(
        BASE + "/session",
        data=json.dumps({"title": "agnc-test"}).encode(),
        method="POST",
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=30) as response:
        session = json.loads(response.read())
    session_id = session["id"]
    print("session", session_id)

    body = {
        "parts": [{"type": "text", "text": "Reply with exactly: OK"}],
        "model": {"providerID": "opencode", "modelID": "big-pickle"},
    }
    req = urllib.request.Request(
        BASE + f"/session/{session_id}/message",
        data=json.dumps(body).encode(),
        method="POST",
        headers={"Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(req, timeout=120) as response:
            payload = json.loads(response.read())
        texts = [part.get("text") for part in payload.get("parts", []) if part.get("type") == "text"]
        print("response", texts[:3])
    except urllib.error.HTTPError as error:
        print("HTTP", error.code, error.read().decode()[:800])


if __name__ == "__main__":
    main()
