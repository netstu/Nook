# QuickJS Recv Echo Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add minimal `recv(callback)` support to the QuickJS runtime so host-side `script.post(...)` can reach loaded scripts, then update the sample `hook.js` into an echo script.

**Architecture:** Keep the current shared QuickJS runtime, but track one persistent recv callback per `script_id`. Extend the runtime bridge so agent-side `SCRIPT_POST` includes `script_id` all the way into the JS dispatcher, and let `recv(callback)` register a handler during script evaluation.

**Tech Stack:** C++17, QuickJS, existing `NookComm` transport and script runtime bridge, host sample script

---
