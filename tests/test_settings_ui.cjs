// Browser smoke test of the actual embedded HTML; HTTP fixtures are not firmware/radio tests.
// npm install --no-save --package-lock=false playwright@1.64.1 && npx playwright install chromium
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs');
const http = require('node:http');
const assert = require('node:assert/strict');
const source = fs.readFileSync('settings_web.cpp', 'utf8');
const pageHTML = source.split('R"HTML(')[1].split(')HTML"')[0];
const received = [];
const state = { descent:.5, climb:1, cap:0, crest:0, target:'Tacx <img src=x onerror=alert(1)>', enabled:true, trainer:true, zwift:true, requested:-800, sent:-400, command_ms:24, heap:100000, min_heap:90000, ap:false, ip:'192.168.1.10', token:'fixture-token' };
const server = http.createServer(async (req,res) => {
  if (req.url === '/api/state') {res.setHeader('Content-Type','application/json');res.end(JSON.stringify(state));return;}
  if (req.method === 'POST') {
    let body=''; for await (const chunk of req) body += chunk;
    received.push({path:req.url,body});
    res.statusCode=req.url === '/api/reboot' ? 409 : 200;
    res.end(req.url === '/api/reboot' ? 'Disconnect Zwift before rebooting' : 'Saved');return;
  }
  res.setHeader('Content-Type','text/html');res.end(pageHTML);
});
(async()=>{
  await new Promise(r=>server.listen(0,'127.0.0.1',r));
  const browser=await chromium.launch({headless:true});
  try {
    const page=await browser.newPage({viewport:{width:390,height:844},isMobile:true,hasTouch:true});
    const errors=[];page.on('pageerror',e=>errors.push(e.message));page.on('dialog',d=>d.accept());
    await page.goto(`http://127.0.0.1:${server.address().port}`);
    await page.waitForFunction(()=>document.getElementById('stats').textContent.includes('-4.00% sent'));
    assert.equal(await page.locator('#target').inputValue(),state.target);
    assert.equal(await page.locator('img').count(),0);
    assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
    await page.locator('#target').fill('Tacx Neo');await page.locator('#descent').fill('0.25');await page.locator('#crest').fill('750');await page.locator('#enabled').uncheck();
    await page.locator('#settings button').click();await page.waitForFunction(()=>document.getElementById('message').textContent==='Saved');
    assert.equal(received[0].path,'/api/settings');for(const value of ['fixture-token','0.25','750','Tacx Neo'])assert(received[0].body.includes(value));
    assert(received[0].body.includes('name="enabled"\r\n\r\n0'));
    await page.locator('#wifi [name=ssid]').fill('Home');await page.locator('#wifi [name=password]').fill('test-only-password');await page.locator('#wifi button').click();
    await page.waitForFunction(()=>document.querySelector('#wifi [name=password]').value==='');assert.equal(received[1].path,'/api/wifi');
    await page.locator('#reboot').click();await page.waitForFunction(()=>document.getElementById('message').textContent.includes('Disconnect Zwift'));
    assert.equal(received[2].path,'/api/reboot');assert.deepEqual(errors,[]);
    if(process.env.SCREENSHOT_PATH)await page.screenshot({path:process.env.SCREENSHOT_PATH,fullPage:true});
    console.log('Mobile settings UI PASS: 390px layout, escaped values, forms/token, password clearing, error feedback');
  } finally {await browser.close();server.close();}
})().catch(e=>{console.error(e);server.close();process.exitCode=1;});
