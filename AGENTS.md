# Verification and publication

- Commit and push task branches and open pull requests as part of authorized work. Routine changes do not require separate permission or maintainer testing before a branch push.
- Run meaningful checks appropriate to the change. Merges must satisfy required CI; keep failures visible and preserve useful coverage rather than weakening checks to get a green result.
- Tags, release publication and release asset uploads require authorization for that release and the relevant automated validation of the candidate artifacts. Reuse authorization already given in the session.
- Require manual desktop or hardware testing only when the changed behavior has a concrete verification gap that automated checks cannot cover. Explain that gap; do not gate workflow, cache or documentation changes on unrelated manual app testing.
- Report the candidate, checks and material verification limits. Do not wait for background CI unless the next required action depends on its result.
