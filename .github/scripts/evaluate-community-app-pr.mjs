const GITHUB_TOKEN = process.env.GITHUB_TOKEN;
const REPO = process.env.GITHUB_REPOSITORY;
const PR_NUMBER = process.env.PULL_REQUEST_NUMBER;
const ANTHROPIC_API_KEY = process.env.ANTHROPIC_API_KEY;

const MAX_TEXT_CHARS = 18000;
const MAX_FILE_BYTES = 256 * 1024;
const REVIEW_MARKER = "<!-- replay-badge-community-app-review -->";

if (!GITHUB_TOKEN || !REPO || !PR_NUMBER) {
  throw new Error("Missing GITHUB_TOKEN, GITHUB_REPOSITORY, or PULL_REQUEST_NUMBER");
}

const [owner, repo] = REPO.split("/");

async function github(path, options = {}) {
  const res = await fetch(`https://api.github.com${path}`, {
    ...options,
    headers: {
      Authorization: `Bearer ${GITHUB_TOKEN}`,
      Accept: "application/vnd.github+json",
      "X-GitHub-Api-Version": "2022-11-28",
      ...(options.headers || {}),
    },
  });
  if (!res.ok) {
    const text = await res.text();
    throw new Error(`GitHub API ${res.status} for ${path}: ${text}`);
  }
  return res.json();
}

function appFolder(path) {
  const match = path.match(/^community_apps\/([^/]+)\//);
  return match ? match[1] : null;
}

function isCommunitySubmissionPath(path) {
  return (
    path.startsWith("community_apps/") ||
    path === "THIRD_PARTY_NOTICES.md" ||
    path.startsWith("licenses/")
  );
}

function isAllowedSubmissionPath(path) {
  return (
    isCommunitySubmissionPath(path) ||
    path.startsWith("docs/") ||
    path.startsWith("firmware/") ||
    path.startsWith("ignition/") ||
    path.startsWith(".github/scripts/") ||
    path.startsWith(".github/workflows/")
  );
}

function needsDocsReview(path) {
  if (path.startsWith("docs/")) return false;
  if (path.startsWith("firmware/docs/")) return false;
  if (path.startsWith("firmware/codeDocs/")) return false;
  if (path.startsWith("firmware/initial_filesystem/docs/")) return false;
  if (path.startsWith("firmware/initial_filesystem/apps/") && path.endsWith("README.md")) return false;
  if (path === "firmware/README.md") return false;
  if (path === "ignition/README.md") return false;
  if (path === "community_apps/README.md") return false;
  if (path === "README.md" || path === "CONTRIBUTING.md" || path === "AGENTS.md") return false;

  return (
    path.startsWith("firmware/src/") ||
    path.startsWith("firmware/micropython/") ||
    path.startsWith("firmware/initial_filesystem/apps/") ||
    path.startsWith("firmware/initial_filesystem/lib/") ||
    path.startsWith("firmware/initial_filesystem/micropython_tests/") ||
    path.startsWith("firmware/scripts/") ||
    path === "firmware/platformio.ini" ||
    path === "firmware/VERSION" ||
    path.startsWith("ignition/") ||
    path.startsWith("community_apps/")
  );
}

function docsUpdated(path) {
  return (
    path.startsWith("docs/") ||
    path.startsWith("firmware/docs/") ||
    path.startsWith("firmware/codeDocs/") ||
    path.startsWith("firmware/initial_filesystem/docs/") ||
    path.endsWith("README.md") ||
    path === "README.md" ||
    path === "CONTRIBUTING.md" ||
    path === "AGENTS.md"
  );
}

function isTextLike(path) {
  return /\.(json|md|py|txt|toml|yaml|yml|csv)$/i.test(path) || !path.includes(".");
}

async function fetchRaw(url) {
  const res = await fetch(url, {
    headers: { Authorization: `Bearer ${GITHUB_TOKEN}` },
  });
  if (!res.ok) return "";
  const text = await res.text();
  return text.length > MAX_TEXT_CHARS
    ? `${text.slice(0, MAX_TEXT_CHARS)}\n\n[... truncated ...]`
    : text;
}

function parseCommunityJson(text) {
  try {
    return { ok: true, value: JSON.parse(text) };
  } catch (err) {
    return { ok: false, error: err.message };
  }
}

function scanPython(path, text) {
  const warnings = [];
  const checks = [
    [/^\s*import\s+(subprocess|ctypes)\b/m, "imports a module that is not expected on badge MicroPython"],
    [/^\s*from\s+(subprocess|ctypes)\b/m, "imports a module that is not expected on badge MicroPython"],
    [/\b(eval|exec)\s*\(/, "uses eval/exec; reviewer should verify this is necessary and safe"],
    [/\b(open|remove|rename)\s*\(\s*["']\/(?!apps\/)/, "touches an absolute path outside /apps; verify it does not overwrite badge-owned files"],
    [/\bwhile\s+True\s*:/, "has an infinite loop; verify it sleeps/yields and honors the exit chord"],
  ];

  for (const [pattern, message] of checks) {
    if (pattern.test(text)) warnings.push(`${path}: ${message}`);
  }
  return warnings;
}

function summarizeStaticReview(files, contents) {
  const changedPaths = files.map((file) => file.filename);
  const appFolders = [...new Set(changedPaths.map(appFolder).filter(Boolean))].sort();
  const errors = [];
  const warnings = [];
  const notes = [];

  const hasCommunityAppChange =
    appFolders.length > 0;

  const unexpected = changedPaths.filter((path) => !isAllowedSubmissionPath(path));
  if (unexpected.length) {
    errors.push(`Unexpected paths changed: ${unexpected.join(", ")}`);
  }

  if (hasCommunityAppChange && appFolders.length === 0) {
    errors.push("No app folder under community_apps/<app>/ was changed.");
  }

  if (appFolders.length > 3) {
    warnings.push("This PR changes more than three app folders; reviewers may want this split into smaller submissions.");
  }

  for (const folder of appFolders) {
    const prefix = `community_apps/${folder}/`;
    const folderPaths = changedPaths.filter((path) => path.startsWith(prefix));
    const hasMain = folderPaths.includes(`${prefix}main.py`);
    const hasMetadata = folderPaths.includes(`${prefix}community.json`);
    const hasReadme = folderPaths.some((path) => path.toLowerCase() === `${prefix}readme.md`);

    if (!hasMain) errors.push(`${folder}: missing ${prefix}main.py.`);
    if (!hasMetadata) errors.push(`${folder}: missing ${prefix}community.json.`);
    if (!hasReadme) warnings.push(`${folder}: README.md is strongly recommended for reviewers and users.`);

    const metadataText = contents.get(`${prefix}community.json`);
    if (metadataText) {
      const parsed = parseCommunityJson(metadataText);
      if (!parsed.ok) {
        errors.push(`${folder}: community.json is invalid JSON: ${parsed.error}`);
      } else {
        for (const key of ["name", "description"]) {
          if (!parsed.value[key] || typeof parsed.value[key] !== "string") {
            errors.push(`${folder}: community.json is missing string field "${key}".`);
          }
        }
        const hasContributorList =
          Array.isArray(parsed.value.contributors) &&
          parsed.value.contributors.every((item) => typeof item === "string") &&
          parsed.value.contributors.length > 0;
        const hasLegacyAuthor = typeof parsed.value.author === "string" && parsed.value.author;
        if (!hasContributorList && !hasLegacyAuthor) {
          errors.push(`${folder}: community.json must include a contributors array or author string.`);
        }
        if (parsed.value.description && parsed.value.description.length > 96) {
          warnings.push(`${folder}: description is ${parsed.value.description.length} chars; keep it short enough for the badge UI.`);
        }
      }
    }
  }

  for (const file of files) {
    if (file.status === "removed") continue;
    if (file.filename.includes("/.") || file.filename.startsWith(".")) {
      warnings.push(`${file.filename}: hidden files are usually not needed in community app submissions.`);
    }
    if (file.size > MAX_FILE_BYTES) {
      warnings.push(`${file.filename}: file is larger than ${MAX_FILE_BYTES} bytes; verify badge storage and download behavior.`);
    }
    if (file.filename.endsWith(".py")) {
      const text = contents.get(file.filename);
      if (text) warnings.push(...scanPython(file.filename, text));
    }
  }

  const docsSensitivePaths = changedPaths.filter(needsDocsReview);
  const docPaths = changedPaths.filter(docsUpdated);
  if (docsSensitivePaths.length && docPaths.length === 0) {
    warnings.push(
      "This PR changes firmware, MicroPython, Ignition, Community Apps, or badge app behavior but does not update docs. Reviewers should confirm whether docs, README files, or user-facing guidance need changes."
    );
  } else if (docsSensitivePaths.length) {
    notes.push(`Docs-related files changed: ${docPaths.join(", ")}.`);
  }

  if (changedPaths.includes("community_apps.json") || changedPaths.includes("firmware/build/community_apps.json")) {
    warnings.push("community_apps.json is generated by GitHub Actions and should not be committed. Commit only app source, metadata, docs, and notices.");
  }
  if (changedPaths.includes("registry/community_apps.json") && !changedPaths.some((p) => p.startsWith("community_apps/"))) {
    warnings.push("registry/community_apps.json is auto-generated — run `python3 firmware/scripts/generate_startup_files.py` and commit it only when community_apps/ or downloadable assets changed.");
  }

  notes.push(`Changed app folder${appFolders.length === 1 ? "" : "s"}: ${appFolders.length ? appFolders.join(", ") : "none detected"}.`);
  if (docsSensitivePaths.length) {
    notes.push(`Docs-sensitive path${docsSensitivePaths.length === 1 ? "" : "s"}: ${docsSensitivePaths.slice(0, 12).join(", ")}${docsSensitivePaths.length > 12 ? ", ..." : ""}.`);
  }

  return { appFolders, errors, warnings, notes };
}

function statusEmoji(errors, warnings) {
  if (errors.length) return "Needs fixes";
  if (warnings.length) return "Needs review";
  return "Looks good";
}

function buildStaticComment(review) {
  const lines = [];
  lines.push(REVIEW_MARKER);
  lines.push("## Community App Submission Review");
  lines.push("");
  lines.push(`**Overall:** ${statusEmoji(review.errors, review.warnings)}`);
  lines.push("");
  lines.push("### Automated Checks");
  lines.push("");
  lines.push(review.errors.length ? "**Blocking issues:**" : "**Blocking issues:** None found.");
  for (const item of review.errors) lines.push(`- ${item}`);
  lines.push("");
  lines.push(review.warnings.length ? "**Reviewer warnings:**" : "**Reviewer warnings:** None found.");
  for (const item of review.warnings) lines.push(`- ${item}`);
  lines.push("");
  lines.push("**Notes:**");
  for (const item of review.notes) lines.push(`- ${item}`);
  lines.push("");
  lines.push("### Human Review Checklist");
  lines.push("");
  lines.push("- App has a clear purpose and works on the badge screen/input constraints.");
  lines.push("- Button labels use the badge UI conventions.");
  lines.push("- The app exits cleanly and does not permanently take over LEDs, files, WiFi, or IR.");
  lines.push("- Licensing is clear and compatible with redistribution through the registry.");
  lines.push("- Any network behavior is intentional, documented, and safe for users.");
  lines.push("- Firmware, MicroPython, Ignition, registry, or badge app behavior changes include matching docs updates, or the PR explains why docs are not needed.");
  lines.push("");
  lines.push("_This workflow inspects PR files without checking out or executing contributor code._");
  return lines.join("\n");
}

async function buildAiReview(pr, files, contents, staticReview) {
  if (!ANTHROPIC_API_KEY) {
    return "\n\n_AI review skipped: `ANTHROPIC_API_KEY` is not configured for this repository._";
  }

  const appText = files
    .filter((file) => file.status !== "removed" && isTextLike(file.filename))
    .slice(0, 20)
    .map((file) => {
      const text = contents.get(file.filename) || "";
      return `### ${file.filename}\n\`\`\`\n${text}\n\`\`\``;
    })
    .join("\n\n");

  const prompt = `You are reviewing a Replay 2026 Badge Community App pull request.

The badge is an ESP32-S3 device with a 128x64 OLED, 8x8 LED matrix, joystick/buttons, haptics, IR, WiFi, and an embedded MicroPython runtime. Community apps are installed from community_apps/<app>/ into /apps/<app>/ on the badge.

Review for:
- Whether the app is useful or fun for badge users.
- Whether the metadata and README make the app understandable.
- Whether the code appears compatible with constrained badge MicroPython.
- Whether it follows badge UI conventions for button labels and clean exit behavior.
- Whether firmware, MicroPython, Ignition, Community Apps, or badge app behavior changes need matching docs updates.
- Safety risks: secrets, destructive file writes, surprising network behavior, huge assets, or unbounded loops.
- Licensing/attribution concerns.

PR title: ${pr.title}
PR body:
${pr.body || "(none)"}

Static review:
${JSON.stringify(staticReview, null, 2)}

Changed text files:
${appText || "(No text files fetched.)"}

Return markdown only, using:
### AI Pass
**Verdict:** Looks good / Needs review / Needs fixes
**Highlights:**
- ...
**Questions for submitter:**
- ...
**Suggested fixes:**
- ...
Keep it concise and practical.`;

  const res = await fetch("https://api.anthropic.com/v1/messages", {
    method: "POST",
    headers: {
      "content-type": "application/json",
      "x-api-key": ANTHROPIC_API_KEY,
      "anthropic-version": "2023-06-01",
    },
    body: JSON.stringify({
      model: "claude-opus-4-5",
      max_tokens: 900,
      messages: [{ role: "user", content: prompt }],
    }),
  });

  if (!res.ok) {
    const text = await res.text();
    return `\n\n_AI review failed: Anthropic API returned ${res.status}: ${text.slice(0, 500)}_`;
  }

  const data = await res.json();
  const text = data.content?.[0]?.text?.trim();
  return text ? `\n\n${text}` : "\n\n_AI review returned no text._";
}

async function upsertComment(body) {
  const comments = await github(`/repos/${owner}/${repo}/issues/${PR_NUMBER}/comments?per_page=100`);
  const existing = comments.find((comment) => comment.body?.includes(REVIEW_MARKER));
  if (existing) {
    await github(`/repos/${owner}/${repo}/issues/comments/${existing.id}`, {
      method: "PATCH",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ body }),
    });
  } else {
    await github(`/repos/${owner}/${repo}/issues/${PR_NUMBER}/comments`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ body }),
    });
  }
}

async function main() {
  const pr = await github(`/repos/${owner}/${repo}/pulls/${PR_NUMBER}`);
  const files = await github(`/repos/${owner}/${repo}/pulls/${PR_NUMBER}/files?per_page=100`);
  const contents = new Map();

  await Promise.all(
    files
      .filter((file) => file.status !== "removed" && file.raw_url && isTextLike(file.filename))
      .slice(0, 40)
      .map(async (file) => {
        contents.set(file.filename, await fetchRaw(file.raw_url));
      })
  );

  const staticReview = summarizeStaticReview(files, contents);
  const staticComment = buildStaticComment(staticReview);
  const aiReview = await buildAiReview(pr, files, contents, staticReview);
  await upsertComment(`${staticComment}${aiReview}`);
}

main().catch(async (err) => {
  console.error(err);
  await upsertComment(`${REVIEW_MARKER}\n## Community App Submission Review\n\nWorkflow failed while evaluating this PR:\n\n\`\`\`\n${err.message}\n\`\`\``).catch(() => {});
  process.exit(1);
});
